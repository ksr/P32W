/* fpdefault.c -- P32W C library: printf's floating conversions left out (see
 * fphook.c): a program that passes no double to a variadic function has no
 * use for them. The archive is searched in name order, a pass at a time,
 * and this member comes before fphook.o: printf, needing __fp_hook, finds
 * it here first -- unless fphook.o came in already, in the first pass, for
 * the __printf_fp a compiled file referred to. */
#include <softfp.h>

int (*__fp_hook)(void (*put)(void *, int), void *arg, int conv, int flags, int width, int prec, const fp64 *x) = 0;
