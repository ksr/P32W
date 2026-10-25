/* bit-fields: layout (sizes and where the bits are, against the host's
   compiler), reading, writing, op=, ++/--, signed and unsigned fields,
   initialisers (static and local, designated), unnamed and zero-width fields */
#include <stdio.h>
#include <string.h>

struct flags { unsigned a : 3, b : 5, c : 1; unsigned d : 23; };
struct mix { int s : 4; unsigned u : 4; int big : 20; short h : 3; unsigned char k; unsigned w : 30, x : 2; };
struct gap { unsigned a : 3; unsigned : 5; unsigned b : 4; unsigned : 0; unsigned c : 2; };
struct cross { unsigned a : 30; unsigned b : 4; };        /* b starts the next unit */
struct shorts { unsigned short p : 7, q : 7, r : 7; };
union uf { unsigned all; struct { unsigned lo : 16, hi : 16; } half; };

static struct flags gf = { 5, 17, 1, 1000000 };
static struct mix gm = { .u = 9, .s = -3, .big = -70000, .h = -2, .k = 200, .w = 0x2ABCDEF0, .x = 3 };
static struct gap gg = { 7, 15, 3 };                      /* the unnamed fields are skipped */
static struct cross gc[2] = { { 1, 2 }, { .b = 3 }, };

static void bytes(const char *what, const void *p, int n)
{
    const unsigned char *b = p;
    int k;
    printf("%s (%d):", what, n);
    for (k = 0; k < n; k++) printf(" %02x", b[k]);
    printf("\n");
}

static struct mix *id(struct mix *p) { printf("[id]"); return p; }

int main(void)
{
    struct flags f;
    struct mix m = { -8, 15, 524287, 3, 7, 1, 0 };
    struct shorts sh = { 100, 50, 127 };
    union uf u;
    struct mix arr[2];
    int k, old;
    printf("sizes %d %d %d %d %d %d\n", (int)sizeof(struct flags), (int)sizeof(struct mix), (int)sizeof(struct gap),
           (int)sizeof(struct cross), (int)sizeof(struct shorts), (int)sizeof(union uf));
    bytes("gf", &gf, sizeof gf); bytes("gm", &gm, sizeof gm); bytes("gg", &gg, sizeof gg); bytes("gc", gc, sizeof gc);
    bytes("m", &m, sizeof m); bytes("sh", &sh, sizeof sh);
    printf("gf %u %u %u %u\n", gf.a, gf.b, gf.c, gf.d);
    printf("gm %d %u %d %d %u %u %u\n", gm.s, gm.u, gm.big, gm.h, gm.k, gm.w, gm.x);
    printf("gg %u %u %u gc %u %u %u %u\n", gg.a, gg.b, gg.c, gc[0].a, gc[0].b, gc[1].a, gc[1].b);
    printf("m %d %u %d %d %u\n", m.s, m.u, m.big, m.h, m.k);
    memset(&f, 0xff, sizeof f);
    f.a = 2; f.b = 31; f.c = 0; f.d = 0x7FFFFF;
    printf("f %u %u %u %u\n", f.a, f.b, f.c, f.d);
    bytes("f", &f, sizeof f);
    f.a = 9;                                              /* truncated to 3 bits: 1 */
    printf("f.a = 9 gives %u; (f.b = 40) is %u\n", f.a, (unsigned)(f.b = 40));
    m.s = 7; m.s += 1;                                    /* wraps to -8 */
    printf("m.s %d\n", m.s);
    m.s = -1; m.s -= 1; printf("m.s %d\n", m.s);
    m.u = 14; old = m.u++; printf("m.u++ %d then %u\n", old, m.u);
    old = m.u++; printf("m.u++ %d then %u\n", old, m.u);  /* 15 -> 0 */
    old = --m.u; printf("--m.u %d\n", old);
    m.big = 1; for (k = 0; k < 18; k++) m.big <<= 1; printf("m.big %d\n", m.big);
    m.big *= 2; printf("m.big %d\n", m.big);              /* 2^19 is out of range: -2^19 */
    m.h = 3; m.h |= 4; printf("m.h %d\n", m.h);
    arr[0] = m; arr[1] = m;
    k = 0;
    arr[k++].u += 3;                                      /* k++ once */
    printf("k %d arr %u %u\n", k, arr[0].u, arr[1].u);
    id(&arr[1])->s += 2;                                  /* id() once */
    printf(" arr[1].s %d\n", arr[1].s);
    old = id(&arr[1])->w--; printf(" w-- %u then %u\n", old, arr[1].w);
    u.all = 0x12345678;
    printf("u %x %x\n", u.half.lo, u.half.hi);
    u.half.hi = 0xBEEF;
    printf("u %x\n", u.all);
    sh.q = sh.p + sh.r;                                   /* 227 -> 7 bits: 99 */
    printf("sh %u %u %u\n", sh.p, sh.q, sh.r);
    {
        struct gap lg = { .b = 9, .c = 1 };
        struct flags lf = { .d = 5, .a = 6 };
        printf("lg %u %u %u lf %u %u %u %u\n", lg.a, lg.b, lg.c, lf.a, lf.b, lf.c, lf.d);
    }
    if (m.u == 0 && gf.c) printf("conditions\n");
    return gf.b + m.u;
}
