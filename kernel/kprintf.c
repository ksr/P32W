/* kprintf.c -- kernel messages on the console, written directly to UART 0
 * and, when the video card is fitted, to its screen (they bypass the
 * terminal driver, so they work before it is running and in a panic).
 * %d %u %x %s %c %p. */
#include "defs.h"
#include <stdarg.h>

void kputc(int c)
{
    if (c == '\n') kputc('\r');
    while (!(IOB(UART0 + 5) & 0x20)) ;
    IOB(UART0) = c;
    vt_putc(c);
}

static void kputs(const char *s) { while (*s) kputc(*s++); }

static void kputn(uint v, int base, int neg, int width)
{
    char b[12];
    int n = 0;
    if (neg) v = -v;
    do { b[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    if (neg) b[n++] = '-';
    while (width-- > n) kputc(base == 16 ? '0' : ' ');
    while (n) kputc(b[--n]);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    int s = splhi();
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        int width = 0;
        if (*fmt != '%') { kputc(*fmt); continue; }
        fmt++;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        switch (*fmt) {
        case 'd': { int v = va_arg(ap, int); kputn(v, 10, v < 0, width); break; }
        case 'u': kputn(va_arg(ap, uint), 10, 0, width); break;
        case 'x': kputn(va_arg(ap, uint), 16, 0, width); break;
        case 'p': kputn(va_arg(ap, uint), 16, 0, 8); break;
        case 's': { char *s = va_arg(ap, char *); kputs(s ? s : "(null)"); break; }
        case 'c': kputc(va_arg(ap, int)); break;
        case '%': kputc('%'); break;
        default: kputc('%'); kputc(*fmt);
        }
    }
    va_end(ap);
    vt_flush();
    splx(s);
}

void panic(const char *msg)
{
    splhi();
    kprintf("\npanic: %s\n", msg);
    halt(1);
}
