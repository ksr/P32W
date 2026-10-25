/* #if arithmetic as C99 says: 64 bits (intmax_t, uintmax_t when unsigned),
   with the usual arithmetic conversions; each line says what the
   preprocessor decided */
#include <stdio.h>
#define T(name) printf("%s yes\n", name)
#define F(name) printf("%s no\n", name)
int main(void)
{
#if 4294967295U > 0
    T("big unsigned positive");
#else
    F("big unsigned positive");
#endif
#if -1 < 0u
    T("-1 < 0u");
#else
    F("-1 < 0u");
#endif
#if (4294967295U >> 16) >= 1
    T("shift of unsigned");
#else
    F("shift of unsigned");
#endif
#if 2147483648 > 0
    T("2^31 positive");
#else
    F("2^31 positive");
#endif
#if 0x7fffffffffffffff + 1 < 0
    T("intmax wraps");
#else
    F("intmax wraps");
#endif
#if -1 / 2 == 0 && -7 % 3 == -1
    T("division toward zero");
#else
    F("division toward zero");
#endif
#if 18446744073709551615 == -1
    T("all ones");
#else
    F("all ones");
#endif
#if (1 ? -1 : 0u) > 0
    T("?: unsigned");
#else
    F("?: unsigned");
#endif
#if 1 << 40 == 1099511627776 && -1 >> 1 == -1
    T("64-bit shifts");
#else
    F("64-bit shifts");
#endif
#if defined(EOF) && !defined(NOT_THERE) && 'A' == 65
    T("defined and chars");
#else
    F("defined and chars");
#endif
    return 0;
}
