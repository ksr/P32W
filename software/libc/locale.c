/* locale.c -- P32W C library: <locale.h> with only the "C" locale (also
 * named "" and "POSIX"): the decimal point is '.', with no grouping. */
#include <locale.h>
#include <string.h>

char *setlocale(int category, const char *locale)
{
    (void)category;
    if (!locale || !*locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX"))
        return "C";
    return 0;
}

struct lconv *localeconv(void)
{
    static struct lconv lc;
    static char point[] = ".", empty[] = "";
    lc.decimal_point = point;
    lc.thousands_sep = lc.grouping = lc.int_curr_symbol = lc.currency_symbol = empty;
    lc.mon_decimal_point = lc.mon_thousands_sep = lc.mon_grouping = empty;
    lc.positive_sign = lc.negative_sign = empty;
    lc.int_frac_digits = lc.frac_digits = lc.p_cs_precedes = lc.p_sep_by_space = 127;   /* (CHAR_MAX: */
    lc.n_cs_precedes = lc.n_sep_by_space = lc.p_sign_posn = lc.n_sign_posn = 127;      /*  not given) */
    return &lc;
}
