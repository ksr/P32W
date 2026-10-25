/* strftime: every conversion of the "C" locale, on dates that test the
   week numbers (ISO 8601's week 53 and week 1 across the new year, %U and
   %W), 12-hour times at midnight and noon, a leap day, and a buffer too
   small; difftime, strcoll, setlocale and localeconv */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <locale.h>

static const char *convs = "aAbBcCdDeFgGhHIjmMnpRrStTuUVwWxXyY%";

int main(void)
{
    static const long when[] = {
        0L,                 /* 1970-01-01 Thursday 00:00 */
        951782400L,         /* 2000-02-29 (leap day) */
        1104537599L,        /* 2004-12-31 23:59:59 (ISO week 53) */
        1104537600L,        /* 2005-01-01 Saturday: still week 53 of 2004 */
        1230681600L,        /* 2008-12-31 Wednesday: ISO week 1 of 2009 */
        1609459200L,        /* 2021-01-01 Friday: week 53 of 2020 */
        1735603200L,        /* 2024-12-31 Tuesday: week 1 of 2025 */
        1791979200L,        /* 2026-10-14 12:00 (noon) */
        2147483647L,        /* 2038-01-19 03:14:07 */
    };
    char buf[200], fmt[4];
    const char *c;
    int k;
    for (k = 0; k < (int)(sizeof when / sizeof when[0]); k++) {
        time_t t = (time_t)when[k];
        struct tm *tm = gmtime(&t);
        printf("%ld:", when[k]);
        for (c = convs; *c; c++) {
            sprintf(fmt, "%%%c", *c);
            strftime(buf, sizeof buf, fmt, tm);
            if (*c == 'n' || *c == 't') printf(" %c=[%d]", *c, buf[0]);
            else printf(" %c=%s", *c, buf);
        }
        printf("\n");
    }
    {
        time_t t = 1791979200L;
        struct tm *tm = gmtime(&t);
        size_t n;
        n = strftime(buf, sizeof buf, "Today is %A, %d %B %Y; it is %I:%M %p.", tm);
        printf("%d \"%s\"\n", (int)n, buf);
        n = strftime(buf, 11, "%Y-%m-%d", tm);          /* fits: 11 bytes with '\0' */
        printf("%d \"%s\"\n", (int)n, buf);
        n = strftime(buf, 10, "%Y-%m-%d", tm);          /* does not: 0 (and buf undefined) */
        printf("%d\n", (int)n);
        n = strftime(buf, sizeof buf, "", tm);
        printf("%d \"%s\"\n", (int)n, buf);
    }
    printf("difftime %.1f %.1f\n", difftime((time_t)100, (time_t)40), difftime((time_t)0, (time_t)86400));
    printf("strcoll %d %d %d\n", strcoll("abc", "abd") < 0, strcoll("b", "a") > 0, strcoll("same", "same") == 0);
    printf("setlocale \"%s\" \"%s\" %s\n", setlocale(LC_ALL, "C"), setlocale(LC_NUMERIC, 0),
           setlocale(LC_ALL, "no-such-locale") ? "found" : "null");
    printf("localeconv \"%s\" \"%s\"\n", localeconv()->decimal_point, localeconv()->thousands_sep);
    printf("CLOCKS_PER_SEC > 0: %d, clock() >= 0: %d\n", CLOCKS_PER_SEC > 0, clock() >= 0);
    return 0;
}
