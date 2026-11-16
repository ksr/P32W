/* core.h -- the DIN 96 shorts tester's logic, apart from its hardware (docs/din96-tester.md, D129).
 *
 * The tester has one pin of a port expander on each of a 96-pin DIN 41612 connector's pins. One at a time it
 * pulls a pin low and reads all 96; with weak pull-ups on them all, a pin that reads low is joined to the one
 * pulled. From those 96 readings: the groups of joined pins, and against a connector type's table (which pins
 * are +5 V, which ground, which may be tied to ground on purpose) what is wrong.
 *
 * Nothing here knows of an Arduino: the sketch gives it t_probe() and the t_say...() functions, and a test on
 * the Mac (emulator/test/din96_test.c) gives it a made-up board.
 *
 * A pin's number here, 0 to 95, is 3 * (its row of the connector - 1) + its column (a = 0, b = 1, c = 2):
 * pin 0 is a1, 1 is b1, 2 is c1, 3 is a2 ... 95 is c32.
 */
#ifndef DIN96_CORE_H
#define DIN96_CORE_H
#include <stdint.h>

#define T_PINS 96
#define T_BYTES 12                      /* 96 bits, bit p of a reading: byte p / 8, bit p % 8 */

/* what a connector type says of a pin */
#define C_SIG 0                         /* a signal: joined to nothing */
#define C_VCC 1                         /* +5 V: joined to every other +5 V pin and to nothing else */
#define C_GND 2                         /* ground: the same */
#define C_SIG_OR_GND 3                  /* a signal that a board may tie to ground on purpose (P32W J3's "I am in" pins) */

/* ---- what the hardware gives ---- */
/* Pull pin p low (p < 96), read all 96 pins into low[] (a 1 bit: that pin reads low), let go. With p = 255
 * pull nothing: only read. */
void t_probe(uint8_t p, uint8_t low[T_BYTES]);
/* ---- what the report is made of ---- */
#ifndef T_S
#define T_S(s) (s)                      /* (the sketch makes this PSTR(s): the texts stay in flash) */
#endif
void t_say(const char *s);              /* a piece of text, T_S("...") */
void t_say_pin(uint8_t p);              /* a pin: its place (b26) and, if the type has one, its name */
void t_say_num(uint8_t n);
void t_say_end(void);                   /* the end of a line */

static uint8_t t_parent[T_PINS];
static uint8_t t_class[T_PINS];         /* the connector type's table, set by the caller before t_test() */

static uint8_t t_root(uint8_t p)
{
    while (t_parent[p] != p) { t_parent[p] = t_parent[t_parent[p]]; p = t_parent[p]; }
    return p;
}
static void t_join(uint8_t a, uint8_t b)
{
    a = t_root(a); b = t_root(b);
    if (a == b) return;
    if (a < b) t_parent[b] = a; else t_parent[a] = b;   /* (a group's root is its lowest pin) */
}
static uint8_t t_bit(const uint8_t m[T_BYTES], uint8_t p) { return (m[p >> 3] >> (p & 7)) & 1; }

static void t_group(uint8_t root)       /* every pin of a group, as text */
{
    uint8_t p, first = 1;
    for (p = 0; p < T_PINS; p++)
        if (t_root(p) == root) { if (!first) t_say(T_S(", ")); t_say_pin(p); first = 0; }
}

/* The group that holds the most pins of a class: the +5 V (or ground) net itself, so that one pin come apart
 * from it is the one reported, and not all the others. 255: the type has no such pins. */
static uint8_t t_most(uint8_t cls)
{
    uint8_t p, q, n, best = 0, root = 255;
    for (p = 0; p < T_PINS; p++) {
        if (t_class[p] != cls || t_root(p) == root) continue;
        for (n = 0, q = 0; q < T_PINS; q++) if (t_class[q] == cls && t_root(q) == t_root(p)) n++;
        if (n > best) { best = n; root = t_root(p); }
    }
    return root;
}

/* One test. Returns the number of faults (0: pass), and reports each as a line. *nvcc and *ngnd: how many pins
 * the +5 V and ground groups have, for the line that says a board passed. */
static uint8_t t_test(uint8_t *nvcc, uint8_t *ngnd)
{
    uint8_t low[T_BYTES], p, q, faults = 0, rv = 255, rg = 255, n;

    for (p = 0; p < T_PINS; p++) t_parent[p] = p;
    /* nothing pulled: every pin must read high. One that does not is held low from outside (a board with
       power on it, or something fitted that should not be) or is a dead pin of the tester */
    t_probe(255, low);
    for (p = 0; p < T_PINS; p++)
        if (t_bit(low, p)) {
            t_say(T_S("held low with nothing pulling it: ")); t_say_pin(p); t_say_end();
            if (++faults == 250) return faults;
        }
    if (faults) return faults;
    for (p = 0; p < T_PINS; p++) {
        t_probe(p, low);
        if (!t_bit(low, p)) {           /* the tester's own pin did not go low: it is fighting something */
            t_say(T_S("cannot be pulled low: ")); t_say_pin(p); t_say_end();
            if (++faults == 250) return faults;
            continue;
        }
        for (q = 0; q < T_PINS; q++)
            if (q != p && t_bit(low, q)) t_join(p, q);
    }
    /* the +5 V pins: one group; the ground pins: one group; the two apart */
    *nvcc = *ngnd = 0;
    rv = t_most(C_VCC); rg = t_most(C_GND);
    if (rv != 255 && rv == rg) {
        t_say(T_S("+5V and GND are joined")); t_say_end(); faults++;
    }
    for (p = 0; p < T_PINS; p++) {
        if (t_class[p] == C_VCC) { if (t_root(p) == rv) (*nvcc)++; else { t_say(T_S("+5V pin not joined to the other +5V pins: ")); t_say_pin(p); t_say_end(); faults++; } }
        if (t_class[p] == C_GND) { if (t_root(p) == rg) (*ngnd)++; else { t_say(T_S("GND pin not joined to the other GND pins: ")); t_say_pin(p); t_say_end(); faults++; } }
        if (faults >= 250) return faults;
    }
    /* a signal in the +5 V group, or in the ground group unless it may be tied to ground */
    for (p = 0; p < T_PINS; p++) {
        uint8_t r = t_root(p), c = t_class[p];
        if (c == C_VCC || c == C_GND) continue;
        if (r == rv && rv != 255) { t_say(T_S("joined to +5V: ")); t_say_pin(p); t_say_end(); faults++; }
        else if (r == rg && rg != 255 && c != C_SIG_OR_GND) { t_say(T_S("joined to GND: ")); t_say_pin(p); t_say_end(); faults++; }
        if (faults >= 250) return faults;
    }
    /* every other group of more than one pin, said once (at its lowest pin): signals joined to each other,
       or to a supply pin that has come apart from its own group */
    for (p = 0; p < T_PINS; p++) {
        if (t_root(p) != p || p == rv || p == rg) continue;
        for (n = 0, q = 0; q < T_PINS; q++) if (t_root(q) == p) n++;
        if (n > 1) { t_say(T_S("joined to each other: ")); t_group(p); t_say_end(); faults++; }
        if (faults >= 250) return faults;
    }
    return faults;
}
#endif
