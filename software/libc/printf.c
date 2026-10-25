/* printf.c -- P32W C library (M2, D22): the printf family on one formatter.
 * Conversions d i u x X o c s p %, flags - 0 + space #, width, precision
 * (also *), length h l (ignored: int and long are both 32 bits). */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <softfp.h>
#include <fplib.h>

typedef struct {
    FILE *f;                   /* a stream, or NULL for a string */
    char *s;
    size_t n, len;             /* string room (n - 1 usable) and count */
} Out;

static void put(Out *o, int c)
{
    if (o->f) fputc(c, o->f);
    else if (o->len + 1 < o->n) o->s[o->len] = c;
    o->len++;
}

static void pad(Out *o, int c, int n) { while (n-- > 0) put(o, c); }
static void fput(void *o, int c) { put((Out *)o, c); }

/* fplib's __fp_print, or 0 in a program that passes no doubles (fphook.c) */
extern int (*__fp_hook)(void (*put)(void *, int), void *arg, int conv, int flags, int width, int prec, const fp64 *x);

static int format(Out *o, const char *fmt, va_list ap)
{
    char num[24];                       /* (a 64-bit number in octal: 22 digits) */
    for (; *fmt; fmt++) {
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1;
        char *s;
        int len, neg, n_, longs = 0;
        unsigned v, base;
        if (*fmt != '%') { put(o, *fmt); continue; }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '+') plus = 1;
            else if (*fmt == ' ') space = 1;
            else if (*fmt == '#') alt = 1;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + *fmt++ - '0';
        }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'L') { if (*fmt == 'l') longs++; fmt++; }   /* (ll: long long) */
        switch (*fmt) {
        case 'c':
            pad(o, ' ', left ? 0 : width - 1);
            put(o, va_arg(ap, int));
            pad(o, ' ', left ? width - 1 : 0);
            break;
        case 's':
            s = va_arg(ap, char *);
            if (!s) s = "(null)";
            len = strlen(s);
            if (prec >= 0 && len > prec) len = prec;
            pad(o, ' ', left ? 0 : width - len);
            n_ = len;
            while (n_--) put(o, *s++);
            pad(o, ' ', left ? width - len : 0);
            break;
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
            double d = va_arg(ap, double);      /* (a float arrives as a double: the same type here) */
            fp64 x;
            memcpy(&x, &d, sizeof x);
            if (__fp_hook)
                __fp_hook(fput, o, *fmt, (left ? FPF_LEFT : 0) | (plus ? FPF_PLUS : 0) | (space ? FPF_SPACE : 0) |
                          (alt ? FPF_ALT : 0) | (zero ? FPF_ZERO : 0), width, prec, &x);
            break;
        }
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            char *digits = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char *prefix = "";
            int n = 0, plen, zeros;
            neg = 0;
            base = (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') ? 16 : *fmt == 'o' ? 8 : 10;
            if (longs >= 2 && *fmt != 'p') {    /* %lld ...: 64 bits */
                unsigned long long w = va_arg(ap, unsigned long long);
                if ((*fmt == 'd' || *fmt == 'i') && (long long)w < 0) { neg = 1; w = -w; }
                do { num[n++] = digits[(int)(w % base)]; w /= base; } while (w);
            } else {
                v = va_arg(ap, unsigned);
                if ((*fmt == 'd' || *fmt == 'i') && (int)v < 0) { neg = 1; v = -v; }
                if (*fmt == 'p') { alt = 1; prec = prec < 0 ? 8 : prec; }
                do { num[n++] = digits[v % base]; v /= base; } while (v);
            }
            if (prec == 0 && n == 1 && num[0] == '0') n = 0;
            if (neg) prefix = "-";
            else if ((*fmt == 'd' || *fmt == 'i') && plus) prefix = "+";
            else if ((*fmt == 'd' || *fmt == 'i') && space) prefix = " ";
            else if (alt && base == 16 && (n || *fmt == 'p')) prefix = *fmt == 'X' ? "0X" : "0x";
            else if (alt && base == 8 && (n == 0 || num[n - 1] != '0')) prefix = "0";
            plen = strlen(prefix);
            zeros = prec > n ? prec - n : 0;
            if (zero && !left && prec < 0 && width > plen + n) zeros = width - plen - n;
            len = plen + zeros + n;
            pad(o, ' ', left ? 0 : width - len);
            while (*prefix) put(o, *prefix++);
            pad(o, '0', zeros);
            while (n) put(o, num[--n]);
            pad(o, ' ', left ? width - len : 0);
            break;
        }
        case '%':
            put(o, '%');
            break;
        case 0:
            return o->len;
        default:
            put(o, '%');
            put(o, *fmt);
        }
    }
    return o->len;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    Out o;
    o.f = f; o.s = NULL; o.n = 0; o.len = 0;
    return format(&o, fmt, ap);
}
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    Out o;
    int r;
    o.f = NULL; o.s = s; o.n = n; o.len = 0;
    r = format(&o, fmt, ap);
    if (n) s[o.len < n ? o.len : n - 1] = 0;
    return r;
}
int vsprintf(char *s, const char *fmt, va_list ap) { return vsnprintf(s, 0x7FFFFFFF, fmt, ap); }
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int printf(const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = vfprintf(stdout, fmt, ap); va_end(ap);
    return r;
}
int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = vfprintf(f, fmt, ap); va_end(ap);
    return r;
}
int sprintf(char *s, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = vsprintf(s, fmt, ap); va_end(ap);
    return r;
}
int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt); r = vsnprintf(s, n, fmt, ap); va_end(ap);
    return r;
}
