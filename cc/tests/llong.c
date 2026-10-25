/* long long: literals, every operator (signed and unsigned), conversions
   both ways, comparisons, shifts, division signs, ++/--/op=, arrays,
   struct members, arguments and results, conditions, constant folding in
   static initialisers; printed without %lld (two hex words, and decimal by
   division) so it checks the arithmetic itself */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

typedef long long i64;
typedef unsigned long long u64;

static i64 g1 = 1LL << 40, g2 = -5, g3 = 0x123456789ABCDEFLL, g4 = (i64)-1 * 3;
static u64 gu = 18446744073709551615ULL;
static i64 tab[4] = { 1, -1, 1LL << 32, -(1LL << 62) };
struct rec { int tag; i64 v; unsigned short s; };

static void hex(const char *what, u64 v)
{
    printf("%s %08x%08x\n", what, (unsigned)(v >> 32), (unsigned)v);
}

static void dec(const char *what, i64 v)
{
    char buf[24];
    int k = 23, neg = v < 0;
    u64 u = neg ? -(u64)v : (u64)v;
    buf[k] = 0;
    do { buf[--k] = '0' + (int)(u % 10); u /= 10; } while (u);
    if (neg) buf[--k] = '-';
    printf("%s %s\n", what, buf + k);
}

static i64 fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
static u64 fib(int n) { u64 a = 0, b = 1; while (n--) { u64 t = a + b; a = b; b = t; } return a; }
static i64 mix(int a, i64 b, unsigned c, i64 d) { return a + b * c - d; }
static struct rec mkrec(i64 v) { struct rec r; r.tag = 7; r.v = v; r.s = 9; return r; }

int main(void)
{
    i64 a = 1234567890123LL, b = -987654321, c;
    u64 u = 0xFEDCBA9876543210ULL, w;
    int k, i = -7;
    unsigned ui = 4000000000u;
    struct rec r;
    hex("g1", g1); hex("g2", g2); hex("g3", g3); hex("g4", g4); hex("gu", gu);
    for (k = 0; k < 4; k++) dec("tab", tab[k]);
    dec("a+b", a + b); dec("a-b", a - b); dec("a*b", a * b); dec("a/b", a / b); dec("a%b", a % b);
    dec("-a/7", -a / 7); dec("-a%7", -a % 7); dec("a/-7", a / -7); dec("a%-7", a % -7);
    hex("u/3", u / 3); hex("u%1000", u % 1000); hex("u*u", u * u); hex("u/big", u / 0x8000000000000001ULL);
    hex("a&u", a & u); hex("a|b", a | b); hex("a^u", a ^ u); hex("~a", ~a); dec("-b", -b);
    for (k = 0; k < 70; k += 13) { hex("u<<k", u << k); hex("u>>k", u >> k); dec("b>>k", b >> k); }
    dec("int+ll", i + a); dec("uint*ll", ui * (i64)3); hex("uint to u64", (u64)ui); dec("int to ll", (i64)i);
    dec("ll to int", (int)a); printf("ll to uint %u, to short %d, to char %d\n", (unsigned)a, (short)a, (char)a);
    printf("compare %d %d %d %d %d %d\n", a > b, a < b, a == a, b != b, b <= -987654321LL, u > (u64)a);
    printf("signedness %d %d\n", (i64)-1 < 0, (u64)-1 > 0);
    printf("truth %d %d %d\n", !a, !(a - a), a && b ? 1 : 0);
    if (u) printf("if u\n");
    k = 0; while (c = 3 - k, c) k++; printf("while k %d\n", k);
    c = 10; c += a; c -= 3; c *= 2; c /= 5; c %= 1000000; c <<= 3; c >>= 1; c |= 1; c &= ~2LL; c ^= 0xF0;
    dec("c", c);
    c = 5; dec("c++", c++); dec("c", c); dec("--c", --c); dec("c--", c--); dec("c", c);
    i = 100; i += a; dec("int += ll", i);
    dec("fact 20", fact(20)); hex("fib 90", fib(90));
    dec("mix", mix(-2, a, 3000000000u, b));
    r = mkrec(a * 2); dec("rec.v", r.v); printf("rec %d %d\n", r.tag, r.s);
    r.v += 1; dec("rec.v+1", mkrec(r.v).v);
    w = 1; for (k = 0; k < 63; k++) w *= 2; hex("2^63", w);
    tab[1] = tab[2] + tab[3]; dec("tab[1]", tab[1]);
    printf("sizes %d %d\n", (int)sizeof(i64), (int)sizeof(tab));
    printf("printf %lld %llu %llx %llX %llo|%20lld|%-22lld|%+lld|%022lld\n", a, u, u, g3, u, b, a, a, b);
    printf("limits %lld %lld %llu %d %ld\n", LLONG_MIN, LLONG_MAX, ULLONG_MAX, 42, 7L);
    {
        char *end;
        errno = 0;
        printf("strtoll %lld %lld", strtoll("  -9223372036854775808x", &end, 10), strtoll("0x7fffffffffffffff", 0, 16));
        printf(" rest %s errno %d\n", end, errno);
        printf("over %lld %d", strtoll("99999999999999999999", 0, 10), errno == ERANGE);
        printf(" ull %llu %llu atoll %lld llabs %lld\n", strtoull("18446744073709551615", 0, 10),
               strtoull("-1", 0, 0), atoll("-1234567890123"), llabs(b));
    }
    return (int)(fact(10) % 256);
}
