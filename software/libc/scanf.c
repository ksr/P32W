/* scanf.c -- P32W C library: formatted input (scanf, fscanf, sscanf and the
 * v forms). One engine reads from a stream or a string through in() and
 * back().
 *
 * Conversions: %d %i %u %o %x %X %c %s %[set] %[^set] %n %p %%, and the
 * floating %e %f %g %a (and their capitals, into a double or a float:
 * one type here; fplib's __fp_scan and __fp_strtod, correctly rounded),
 * with a width, * (read but do not store) and the size letters hh h l L
 * (ints and longs are both 32 bits here). */
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <softfp.h>
#include <fplib.h>

struct src {
    FILE *f;                    /* a stream, or */
    const char *s;              /* a string */
    int n;                      /* characters consumed (for %n) */
};

static int in(struct src *r)
{
    int c;
    if (r->f) c = getc(r->f);
    else c = *r->s ? (unsigned char)*r->s++ : EOF;
    if (c != EOF) r->n++;
    return c;
}

static void back(struct src *r, int c)
{
    if (c == EOF) return;
    r->n--;
    if (r->f) ungetc(c, r->f);
    else r->s--;
}

/* (for fplib's __fp_scan, which reads a floating number from the source) */
static int sc_get(void *p) { return in((struct src *)p); }
static void sc_back(int c, void *p) { back((struct src *)p, c); }

static int digit(int c, int base)
{
    int d;
    if (isdigit(c)) d = c - '0';
    else if (isalpha(c)) d = tolower(c) - 'a' + 10;
    else return -1;
    return d < base ? d : -1;
}

static int engine(struct src *r, const char *fmt, va_list ap)
{
    int count = 0, c;
    for (; *fmt; fmt++) {
        int skip = 0, width = 0, size = 0, conv;
        if (isspace((unsigned char)*fmt)) {         /* white space: any amount, none included */
            while (isspace(c = in(r)))
                ;
            back(r, c);
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {         /* an ordinary character must match */
            if (*fmt == '%') fmt++;
            if (*fmt == '%') { while (isspace(c = in(r))) ; } else c = in(r);
            if (c != (unsigned char)*fmt) { back(r, c); return c == EOF && !count ? EOF : count; }
            continue;
        }
        fmt++;
        if (*fmt == '*') { skip = 1; fmt++; }
        while (isdigit((unsigned char)*fmt)) width = width * 10 + *fmt++ - '0';
        while (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') size = *fmt++ == 'h' ? (size == 'h' ? 'H' : 'h') : 'l';
        conv = *fmt;
        if (!conv) break;
        if (conv == 'n') {                          /* (not counted) */
            if (!skip) *va_arg(ap, int *) = r->n;
            continue;
        }
        if (conv != 'c' && conv != '[') {           /* every other one skips white space first */
            while (isspace(c = in(r)))
                ;
            back(r, c);
        }
        if (conv == 'c') {
            char *p = skip ? 0 : va_arg(ap, char *);
            int k;
            if (!width) width = 1;
            for (k = 0; k < width; k++) {
                if ((c = in(r)) == EOF) return count ? count : EOF;
                if (p) *p++ = c;
            }
            if (!skip) count++;
        } else if (conv == 's' || conv == '[') {
            char set[256], *p = skip ? 0 : va_arg(ap, char *);
            int k = 0, i, neg = 0;
            if (conv == '[') {                      /* the set: ] first is a member; a-z is a range */
                fmt++;
                if (*fmt == '^') { neg = 1; fmt++; }
                for (i = 0; i < 256; i++) set[i] = neg;
                if (*fmt == ']') { set[']'] = !neg; fmt++; }
                for (; *fmt && *fmt != ']'; fmt++) {
                    if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                        for (i = (unsigned char)fmt[0]; i <= (unsigned char)fmt[2]; i++) set[i] = !neg;
                        fmt += 2;
                    } else set[(unsigned char)*fmt] = !neg;
                }
                if (!*fmt) return count;           /* (an unclosed set) */
            }
            while (!width || k < width) {
                c = in(r);
                if (c == EOF) break;
                if (conv == 's' ? isspace(c) : !set[c]) { back(r, c); break; }
                if (p) *p++ = c;
                k++;
            }
            if (!k) return c == EOF && !count ? EOF : count;
            if (p) { *p = 0; count++; }
        } else if (strchr("eEfFgGaA", conv)) {      /* a floating number (float * and double *: the same) */
            char fb[513];
            fp64 v;
            if ((c = in(r)) == EOF) return count ? count : EOF;
            back(r, c);
            if (!__fp_scan(sc_get, sc_back, r, fb, sizeof fb, width)) return count;
            __fp_strtod(&v, fb);
            if (!skip) {
                memcpy(va_arg(ap, double *), &v, 8);
                count++;
            }
        } else {                                    /* the integers */
            unsigned long v = 0;
            int base, neg = 0, any = 0, d;
            switch (conv) {
            case 'd': case 'u': base = 10; break;
            case 'o': base = 8; break;
            case 'x': case 'X': case 'p': base = 16; break;
            case 'i': base = 0; break;
            default: return count;                  /* (an unknown conversion) */
            }
            if (!width) width = -1;
            c = in(r);
            if ((c == '-' || c == '+') && width) {
                neg = c == '-';
                if (width > 0) width--;
                c = in(r);
            }
            if (c == '0' && width) {                /* 0, or the start of 0x or an octal number */
                any = 1;
                if (width > 0) width--;
                c = in(r);
                if ((base == 0 || base == 16) && (c == 'x' || c == 'X') && width) {
                    int c2;
                    if (width > 0) width--;
                    c2 = in(r);
                    if (digit(c2, 16) >= 0) { base = 16; any = 0; c = c2; }
                    else { back(r, c2); c = EOF; width = 0; }  /* "0x" alone: 0, the x read (one pushback) */
                } else if (base == 0) base = 8;
            }
            if (base == 0) base = 10;
            while (width && (d = digit(c, base)) >= 0) {
                v = v * base + d;
                any = 1;
                if (width > 0) width--;
                c = in(r);
            }
            back(r, c);
            if (!any) return c == EOF && !count ? EOF : count;
            if (neg) v = -v;
            if (!skip) {
                if (conv == 'p') *va_arg(ap, void **) = (void *)v;
                else if (size == 'H') *va_arg(ap, char *) = v;
                else if (size == 'h') *va_arg(ap, short *) = v;
                else *va_arg(ap, int *) = v;   /* (long is int-sized) */
                count++;
            }
        }
    }
    return count;
}

int vfscanf(FILE *f, const char *fmt, va_list ap)
{
    struct src r;
    r.f = f; r.s = 0; r.n = 0;
    return engine(&r, fmt, ap);
}

int vsscanf(const char *s, const char *fmt, va_list ap)
{
    struct src r;
    r.f = 0; r.s = s; r.n = 0;
    return engine(&r, fmt, ap);
}

int vscanf(const char *fmt, va_list ap) { return vfscanf(stdin, fmt, ap); }

int fscanf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfscanf(f, fmt, ap);
    va_end(ap);
    return n;
}

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsscanf(s, fmt, ap);
    va_end(ap);
    return n;
}

int scanf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return n;
}
