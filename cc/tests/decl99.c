/* declarations MicroPython's sources use (D35): restrict (accepted,
   ignored), a parameter or local named like a typedef (qstr qstr), and
   offsetof as an integer constant expression (array sizes, case labels,
   static initialisers, enum values) */
#include <stdio.h>
#include <stddef.h>
#include <string.h>

typedef unsigned qstr;
typedef struct { char tag; int len; double d; const char *s; } item_t;
typedef struct { char tag; int len; double d; const char *items; } other_t;

/* a static assertion as MicroPython writes it: a negative array size if the
   layouts differ */
#define SAME(a, b) ((void)sizeof(char[1 - 2 * !(offsetof(item_t, a) == offsetof(other_t, b))]))

enum { OFF_LEN = offsetof(item_t, len), OFF_S = offsetof(item_t, s) };
static const size_t offs[] = { offsetof(item_t, tag), offsetof(item_t, len), offsetof(item_t, d), offsetof(item_t, s) };
static char pad[offsetof(item_t, d)];

static qstr twice(qstr qstr) { return qstr * 2; }

static void copy(char *restrict dst, const char *restrict src, size_t n)
{
    while (n--) *dst++ = *src++;
}

static const char *which(size_t off)
{
    switch (off) {
    case offsetof(item_t, len): return "len";
    case offsetof(item_t, s): return "s";
    default: return "other";
    }
}

int main(void)
{
    char buf[16];
    qstr q = 21;
    int *restrict p = (int *)&q;
    SAME(len, len);
    SAME(s, items);
    {
        qstr qstr = 5;                  /* a local named like the typedef */
        printf("twice %u %u\n", twice(q), twice(qstr));
    }
    copy(buf, "restrict", 9);
    printf("%s %d\n", buf, *p);
    printf("offsets %d %d %d %d, enum %d %d, pad %d\n", (int)offs[0], (int)offs[1], (int)offs[2], (int)offs[3],
           OFF_LEN, OFF_S, (int)sizeof pad);
    printf("switch %s %s %s\n", which(offsetof(item_t, len)), which(offsetof(item_t, s)), which(0));
    return 0;
}
