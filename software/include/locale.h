/* locale.h -- P32W C library: only the "C" locale (libc/locale.c). */
#ifndef _LOCALE_H
#define _LOCALE_H

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

struct lconv {
    char *decimal_point, *thousands_sep, *grouping;
    char *int_curr_symbol, *currency_symbol, *mon_decimal_point;
    char *mon_thousands_sep, *mon_grouping, *positive_sign, *negative_sign;
    char int_frac_digits, frac_digits, p_cs_precedes, p_sep_by_space;
    char n_cs_precedes, n_sep_by_space, p_sign_posn, n_sign_posn;
};

char *setlocale(int category, const char *locale);  /* "C" (also "" and "POSIX"); else 0 */
struct lconv *localeconv(void);

#endif
