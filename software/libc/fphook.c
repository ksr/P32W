/* fphook.c -- P32W C library: printf's floating conversions, linked only
 * into programs that use them (D30). The compilers put a reference to
 * __printf_fp into any file that passes a double to a variadic function,
 * which brings in this member, and with it fplib's formatter; every other
 * program gets fpdefault.o's null hook, and printf stays small (see there
 * for how the archive's name order makes this work). */
#include <softfp.h>
#include <fplib.h>

int __printf_fp;
int (*__fp_hook)(void (*put)(void *, int), void *arg, int conv, int flags, int width, int prec, const fp64 *x) = __fp_print;
