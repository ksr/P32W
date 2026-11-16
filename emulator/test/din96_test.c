/* din96_test.c -- the DIN 96 shorts tester's logic (hardware/din96-tester/firmware/din96_tester/core.h, D129)
 * against made-up boards, on the host: what its Nano would report for each. In make test.
 *
 *   cc -I ../../hardware/din96-tester/firmware/din96_tester din96_test.c -o din96_test && ./din96_test
 */
#include <stdio.h>
#include <string.h>
#include "core.h"

#define MAXG 8
static int grp[MAXG][T_PINS], glen[MAXG], ngrp;          /* the board: groups of joined pins */
static int heldlow = -1, deadpin = -1;                  /* a pin held low from outside; a tester pin that cannot pull */
static char out[8192];
static int nchecks, nbad;

static int pin(char col, int row) { return 3 * (row - 1) + (col - 'a'); }
static int isset(const uint8_t *m, int p) { return (m[p >> 3] >> (p & 7)) & 1; }

void t_probe(uint8_t p, uint8_t low[T_BYTES])
{
    int g, k, more = 1;
    memset(low, 0, T_BYTES);
    if (heldlow >= 0) low[heldlow >> 3] |= 1 << (heldlow & 7);
    if (p == 255 || p == deadpin) return;
    low[p >> 3] |= 1 << (p & 7);
    while (more)                                        /* everything joined to it, through any chain of joins */
        for (more = 0, g = 0; g < ngrp; g++) {
            int any = 0, all = 1;
            for (k = 0; k < glen[g]; k++) { if (isset(low, grp[g][k])) any = 1; else all = 0; }
            if (any && !all) { for (k = 0; k < glen[g]; k++) low[grp[g][k] >> 3] |= 1 << (grp[g][k] & 7); more = 1; }
        }
}
void t_say(const char *s) { strncat(out, s, sizeof out - strlen(out) - 1); }
void t_say_pin(uint8_t p) { char b[8]; snprintf(b, sizeof b, "%c%d", 'a' + p % 3, p / 3 + 1); t_say(b); }
void t_say_num(uint8_t n) { char b[8]; snprintf(b, sizeof b, "%d", n); t_say(b); }
void t_say_end(void) { t_say("\n"); }

static void p32w(const char *conn)                      /* P32W's J1, J2 or J3, as docs/bus.md has their supply pins */
{
    int p;
    for (p = 0; p < T_PINS; p++) {
        int row = p / 3 + 1; char col = 'a' + p % 3;
        uint8_t c = C_SIG;
        if (row <= 2) c = C_VCC;
        else if (row >= 31) c = C_GND;
        else if (col == 'b' && row % 2 == 1) c = C_GND;
        else if (col == 'b' && !strcmp(conn, "J2")) c = C_VCC;
        if (!strcmp(conn, "J3") && col == 'b' && (row == 28 || row == 30)) c = C_SIG_OR_GND;
        t_class[p] = c;
    }
}
static void good(void)                                  /* the supply pins joined as the type says, nothing else */
{
    int p;
    ngrp = 2; glen[0] = glen[1] = 0; heldlow = deadpin = -1;
    for (p = 0; p < T_PINS; p++) {
        if (t_class[p] == C_VCC) grp[0][glen[0]++] = p;
        if (t_class[p] == C_GND) grp[1][glen[1]++] = p;
    }
}
static void join2(int a, int b) { grp[ngrp][0] = a; grp[ngrp][1] = b; glen[ngrp++] = 2; }
static int run(uint8_t *nv, uint8_t *ng) { out[0] = 0; return t_test(nv, ng); }
static void check(const char *what, int faults, int want, const char *must)
{
    nchecks++;
    if (faults != want || (must && !strstr(out, must))) { nbad++; printf("FAILED: %s: %d faults (want %d)\n%s", what, faults, want, out); }
}

int main(void)
{
    uint8_t nv, ng;
    int f, a, b, missed = 0, lone;

    p32w("J2"); good(); f = run(&nv, &ng); check("a good J2", f, 0, 0);
    nchecks++; if (nv != 20 || ng != 20) { nbad++; printf("FAILED: J2's supply groups %d and %d pins\n", nv, ng); }
    p32w("J1"); good(); f = run(&nv, &ng); check("a good J1", f, 0, 0);
    nchecks++; if (nv != 6 || ng != 20) { nbad++; printf("FAILED: J1's supply groups %d and %d pins\n", nv, ng); }
    join2(pin('c', 26), pin('b', 26));
    f = run(&nv, &ng); check("two signals joined", f, 1, "joined to each other: b26, c26");
    good(); grp[ngrp][0] = pin('a', 10); grp[ngrp][1] = pin('a', 11); grp[ngrp][2] = pin('a', 12); glen[ngrp++] = 3;
    f = run(&nv, &ng); check("three signals joined", f, 1, "a10, a11, a12");
    good(); grp[1][glen[1]++] = pin('a', 30);
    f = run(&nv, &ng); check("a signal on ground", f, 1, "joined to GND: a30");
    good(); grp[0][glen[0]++] = pin('c', 3);
    f = run(&nv, &ng); check("a signal on +5 V", f, 1, "joined to +5V: c3");
    good(); join2(pin('a', 1), pin('a', 32));
    f = run(&nv, &ng); check("+5 V on ground", f, 1, "+5V and GND are joined");
    good(); memmove(&grp[1][0], &grp[1][1], sizeof(int) * --glen[1]);       /* the first ground pin not soldered */
    f = run(&nv, &ng); check("a ground pin apart", f, 1, "GND pin not joined to the other GND pins: b3");
    good(); lone = grp[0][--glen[0]]; join2(lone, pin('a', 3));
    f = run(&nv, &ng); check("a +5 V pin apart, and on a signal", f, 2, "joined to each other: ");
    p32w("J3"); good(); grp[1][glen[1]++] = pin('b', 28);
    f = run(&nv, &ng); check("J3: -CPUIN on ground, as the CPU card has it", f, 0, 0);
    good(); grp[1][glen[1]++] = pin('b', 26);
    f = run(&nv, &ng); check("J3: another signal on ground", f, 1, "joined to GND: b26");
    good(); join2(pin('b', 28), pin('b', 30));
    f = run(&nv, &ng); check("J3: the two 'I am in' pins joined to each other", f, 1, "joined to each other: b28, b30");
    p32w("J1"); good(); heldlow = pin('a', 5);
    f = run(&nv, &ng); check("a pin held low from outside", f, 1, "held low with nothing pulling it: a5");
    good(); deadpin = pin('c', 9);
    f = run(&nv, &ng); check("a pin of the tester that cannot pull", f, 1, "cannot be pulled low: c9");
    memset(t_class, C_SIG, sizeof t_class); ngrp = 0; heldlow = deadpin = -1;       /* TYPE 0: nothing may be joined */
    f = run(&nv, &ng); check("nothing plugged in", f, 0, 0);
    join2(0, 95);
    f = run(&nv, &ng); check("type 0: any join is a fault", f, 1, "a1, c32");
    p32w("J2");                                         /* every pair of pins joined, one pair at a time */
    for (a = 0; a < T_PINS; a++)
        for (b = a + 1; b < T_PINS; b++) {
            int same = t_class[a] == t_class[b] && t_class[a] != C_SIG;     /* (two pins of one supply: no fault) */
            good(); join2(a, b);
            f = run(&nv, &ng);
            if ((f == 0) != same) missed++;
        }
    nchecks++; if (missed) { nbad++; printf("FAILED: %d of the 4560 pairs judged wrongly\n", missed); }
    if (nbad) { printf("din96 tester: %d of %d checks FAILED\n", nbad, nchecks); return 1; }
    printf("DIN 96 tester's logic: %d checks on made-up boards, every one of the 4560 pairs of pins among them\n", nchecks);
    return 0;
}
