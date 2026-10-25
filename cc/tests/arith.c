/* integer arithmetic, conversions and comparisons */
#include <stdio.h>
int vals[] = { 0, 1, -1, 7, -7, 13, -13, 2147483647, -2147483647 - 1, 65535, -32768, 255, 100000 };
unsigned uv[] = { 0, 1, 7, 13, 0x80000000u, 0xFFFFFFFFu, 3000000000u, 65535 };
int main(void)
{
    int i, j, n = sizeof vals / sizeof vals[0], m = sizeof uv / sizeof uv[0];
    unsigned h = 0;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            int a = vals[i], b = vals[j];
            h = h * 31 + (a + b); h = h * 31 + (a - b); h = h * 31 + (a * b);
            h = h * 31 + (a & b); h = h * 31 + (a | b); h = h * 31 + (a ^ b);
            h = h * 31 + (a < b) + 2 * (a <= b) + 4 * (a > b) + 8 * (a >= b) + 16 * (a == b) + 32 * (a != b);
            if (b != 0 && !(a == -2147483647 - 1 && b == -1)) { h = h * 31 + a / b; h = h * 31 + a % b; }
            h = h * 31 + (a << (b & 31)); h = h * 31 + (a >> (b & 31));
        }
    printf("signed hash %u\n", h);
    h = 0;
    for (i = 0; i < m; i++)
        for (j = 0; j < m; j++) {
            unsigned a = uv[i], b = uv[j];
            h = h * 31 + (a + b); h = h * 31 + (a - b); h = h * 31 + (a * b);
            h = h * 31 + (a < b) + 2 * (a <= b) + 4 * (a > b) + 8 * (a >= b);
            if (b) { h = h * 31 + a / b; h = h * 31 + a % b; }
            h = h * 31 + (a >> (b & 31));
        }
    printf("unsigned hash %u\n", h);
    {
        char c = 200; unsigned char uc = 200; short s = 40000; unsigned short us = 40000;
        int x = -1; unsigned ux = x;
        printf("%d %d %d %d\n", c, uc, s, us);
        printf("%d %d %d\n", (char)300, (unsigned char)-1, (short)70000);
        printf("%d %d\n", -1 < 1u, x < 0 && ux > 0);
        printf("%d %d %d %d\n", 7 / -2, 7 % -2, -7 / 2, -7 % 2);
        printf("%u %d\n", (unsigned)-5 >> 1, -5 >> 1);
        c = 127; c++; printf("%d\n", c);
        uc = 0; uc--; printf("%d\n", uc);
        s = -1; us = s; printf("%d %d\n", s, us);
        x = 5; x += 3; x -= 1; x *= 6; x /= 4; x %= 7; x <<= 3; x >>= 1; x |= 64; x &= ~2; x ^= 0x55;
        printf("%d\n", x);
        printf("%d %d %d\n", 1 + 2 * 3 - 4 / 2, (1 + 2) * (3 - 4) / 2, 1 << 2 + 3);
        printf("%d %d %d %d\n", !0, !5, ~0, -(-3));
        printf("%d\n", 017 + 0x1f + 'A' + '\n' + '\0' + '\x41' + '\101');
    }
    return 0;
}
