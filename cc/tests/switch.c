/* switch: jump tables (dense cases) and compare chains (sparse ones), with
   holes, default and no default, values below and above the range,
   negative and unsigned ranges, fall-through, nesting, and break/continue */
#include <stdio.h>

static const char *dense(int x)             /* 0..7 with a hole at 5 */
{
    switch (x) {
    case 0: return "zero";
    case 1: return "one";
    case 2: return "two";
    case 3: return "three";
    case 4: return "four";
    case 6: return "six";
    case 7: return "seven";
    default: return "other";
    }
}

static int nodefault(int x)                 /* 10..14: no default, falls out */
{
    int r = -1;
    switch (x) {
    case 10: r = 100; break;
    case 11: r = 110; break;
    case 12: r = 120;                       /* falls through */
    case 13: r += 1; break;
    case 14: r = 140; break;
    }
    return r;
}

static int negative(int x)                  /* -3..2 */
{
    switch (x) {
    case -3: return 30;
    case -2: return 20;
    case -1: return 10;
    case 0: return 0;
    case 1: return -10;
    case 2: return -20;
    }
    return 99;
}

static int far(int x)                       /* a range that needs LI */
{
    switch (x) {
    case 100000: return 1;
    case 100001: return 2;
    case 100002: return 3;
    case 100004: return 4;
    default: return 0;
    }
}

static int uns(unsigned x)                  /* across 2^31 */
{
    switch (x) {
    case 0x7FFFFFFEu: return 1;
    case 0x7FFFFFFFu: return 2;
    case 0x80000000u: return 3;
    case 0x80000001u: return 4;
    default: return 0;
    }
}

static int top(unsigned x)                  /* the top of the unsigned range */
{
    switch (x) {
    case 0xFFFFFFFCu: return 1;
    case 0xFFFFFFFDu: return 2;
    case 0xFFFFFFFEu: return 3;
    case 0xFFFFFFFFu: return 4;
    }
    return 0;
}

static int sparse(int x)                    /* stays a compare chain */
{
    switch (x) {
    case 1: return 1;
    case 100: return 2;
    case 1000: return 3;
    case 10000: return 4;
    case -5: return 5;
    }
    return 0;
}

static int letter(char c)
{
    switch (c) {
    case 'a': case 'e': case 'i': case 'o': case 'u': return 'V';
    case 'b': case 'c': case 'd': case 'f': case 'g': case 'h': return 'C';
    default: return '?';
    }
}

static int nested(int a, int b)
{
    switch (a) {
    case 0: case 1: case 2: case 3:
        switch (b) {
        case 0: return a * 10;
        case 1: return a * 10 + 1;
        case 2: return a * 10 + 2;
        case 3: break;
        default: return -1;
        }
        return a * 100;
    case 4: return 4000;
    }
    return -2;
}

/* an interpreter loop, the case jump tables are for */
enum { PUSH, ADD, SUB, MUL, DUP, SWAP, JNZ, DEC, PRINT, HALT };
static int run(const int *code)
{
    int stack[16], sp = 0, pc = 0, steps = 0, t;
    for (;;) {
        steps++;
        switch (code[pc++]) {
        case PUSH: stack[sp++] = code[pc++]; continue;
        case ADD: sp--; stack[sp - 1] += stack[sp]; break;
        case SUB: sp--; stack[sp - 1] -= stack[sp]; break;
        case MUL: sp--; stack[sp - 1] *= stack[sp]; break;
        case DUP: stack[sp] = stack[sp - 1]; sp++; break;
        case SWAP: t = stack[sp - 1]; stack[sp - 1] = stack[sp - 2]; stack[sp - 2] = t; break;
        case JNZ: if (stack[--sp]) pc = code[pc]; else pc++; break;
        case DEC: stack[sp - 1]--; break;
        case PRINT: printf(" %d", stack[--sp]); break;
        case HALT: printf(" (%d steps)\n", steps); return sp;
        default: printf(" bad op\n"); return -1;
        }
    }
}

int main(void)
{
    int k;
    /* count down from 5, then (6*7-2)*2, then 2-1 by SWAP */
    static const int prog[] = { PUSH, 5, DUP, PRINT, DEC, DUP, JNZ, 2,
        PUSH, 6, PUSH, 7, MUL, PUSH, 2, SUB, DUP, ADD, PRINT,
        PUSH, 1, PUSH, 2, SWAP, SUB, PRINT, HALT };
    for (k = -2; k <= 9; k++) printf("%s ", dense(k));
    printf("\n");
    for (k = 8; k <= 16; k++) printf("%d ", nodefault(k));
    printf("\n");
    for (k = -5; k <= 4; k++) printf("%d ", negative(k));
    printf("\n");
    for (k = 99998; k <= 100006; k++) printf("%d ", far(k));
    printf("\n");
    printf("%d %d %d %d %d %d\n", uns(0x7FFFFFFDu), uns(0x7FFFFFFEu), uns(0x7FFFFFFFu),
           uns(0x80000000u), uns(0x80000001u), uns(0x80000002u));
    printf("%d %d %d %d %d\n", top(0), top(0xFFFFFFFBu), top(0xFFFFFFFCu), top(0xFFFFFFFEu), top(0xFFFFFFFFu));
    for (k = -6; k <= 10001; k += (k < 2 ? 1 : k < 1001 ? 99 : 9000)) printf("%d ", sparse(k));
    printf("\n");
    for (k = 'a'; k <= 'j'; k++) putchar(letter((char)k));
    putchar(letter(0)); putchar(letter('z'));
    printf("\n");
    for (k = 0; k < 6; k++) printf("%d %d %d %d %d ", nested(k, 0), nested(k, 1), nested(k, 2), nested(k, 3), nested(k, 9));
    printf("\n");
    run(prog);
    {
        static const int bad[] = { PUSH, 3, 42 };
        run(bad);
    }
    return 0;
}
