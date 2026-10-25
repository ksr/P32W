/* isamix.c -- measures candidate ISA additions on an instruction trace
 * (p32uemu -I), streamed through a FIFO by tools/isamix.sh: per candidate,
 * how often its pattern runs and the cycles a fused instruction would save.
 * Records: four little-endian u32: pc, ir, cycles, flags (bit 0 supervisor).
 * Liveness is dynamic: a register is dead after i if the trace writes it
 * before reading it (within HORIZON instructions; otherwise live).
 *
 *     cc -O2 -I emulator -o isamix tools/isamix.c   (isamix.sh does this) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "p32opc.h"

#define RING 4096
#define HORIZON 400
#define LOOK 8                       /* the patterns span at most this many */
typedef struct { uint32_t pc, ir, cy, fl; } rec;
static rec R[RING];
static unsigned char used[RING];
static unsigned long long n, head;    /* records read; next to process */

static rec *at(unsigned long long i) { return &R[i % RING]; }
static unsigned op(unsigned long long i) { return at(i)->ir >> 24; }
static unsigned fA(unsigned long long i) { return (at(i)->ir >> 20) & 15; }
static unsigned fB(unsigned long long i) { return (at(i)->ir >> 16) & 15; }
static unsigned fC(unsigned long long i) { return (at(i)->ir >> 12) & 15; }
static unsigned imm(unsigned long long i) { return at(i)->ir & 0xFFFF; }
static int seq(unsigned long long i) { return i + 1 < n && at(i + 1)->pc == at(i)->pc + 4; }

/* registers read / written as bit masks; -1 = a barrier (treat all live) */
static int rw(unsigned long long i, unsigned *rd, unsigned *wr)
{
    unsigned o = op(i), f = p32_fmt[o], a = 1u << fA(i), b = 1u << fB(i), c = 1u << fC(i), sp = 1u << 15;
    *rd = *wr = 0;
    switch (f) {
    case F_R: *rd = b | c; *wr = a; return 0;
    case F_I: case F_IU: case F_SH: *rd = b; *wr = a; return 0;
    case F_LUI: *wr = a; return 0;
    case F_LD: *rd = b | (o == OP_SWAP ? a : 0); *wr = a; return 0;
    case F_ST: case F_BR: *rd = a | b; return 0;
    }
    switch (o) {
    case OP_JMP: return 0;
    case OP_CALL: case OP_RET: *rd = sp; *wr = sp; return 0;
    case OP_JR: *rd = b; return 0;
    case OP_CALLR: *rd = b | sp; *wr = sp; return 0;
    case OP_MFCR: *wr = a; return 0;
    case OP_MTCR: *rd = a; return 0;
    }
    return -1;
}

static int dead_after(unsigned long long i, unsigned reg)
{
    if (reg == 0) return 1;
    for (unsigned long long j = i + 1; j < n && j <= i + HORIZON; j++) {
        unsigned rd, wr;
        if (rw(j, &rd, &wr) < 0) return 0;
        if (rd & (1u << reg)) return 0;
        if (wr & (1u << reg)) return 1;
    }
    return 0;
}

static int isload(unsigned o) { return o == OP_LB || o == OP_LH || o == OP_LW || o == OP_LBU || o == OP_LHU; }
static int isstore(unsigned o) { return o == OP_SB || o == OP_SH || o == OP_SW; }
static int isbranch(unsigned o) { return p32_fmt[o] == F_BR; }

/* j: a load/store with base x, offset 0, x dead after it (or the load's own target) */
static int memop0(unsigned long long j, unsigned x)
{
    unsigned o = op(j);
    if (!(isload(o) || isstore(o)) || fB(j) != x || imm(j) != 0) return 0;
    if (isstore(o) && fA(j) == x) return 0;
    return (isload(o) && fA(j) == x) || dead_after(j, x);
}

enum { C_SCALED, C_INDEXED, C_ADDSL, C_FOLDADDI, C_LAMEM, C_RELOAD, C_LI32, C_BRIMM, C_LEAF, C_STM, C_LDM, C_POSTINC, NCAND };
static const char *cname[NCAND] = {
    "mem [rs + rt << k]: scaled indexed load / store",
    "mem [rs + rt]: indexed load / store",
    "ADDSL rd = rs + (rt << k): an ALU function",
    "[compiler] ADDI folded into the load / store offset",
    "[compiler] LA + mem 0 -> LUI + mem lo",
    "[compiler] the same word loaded twice in a row",
    "LI32: LUI + ORI as one two-word instruction",
    "Bcc ra, simm8, label (compare with a constant)",
    "leaf calls with a link register (JAL / JR lr)",
    "STM: store registers (prologue), 2 cycles each",
    "LDM: load registers (epilogue), 2 cycles each",
    "mem off(rs)+k: post-increment load / store (+1 cycle)",
};
static unsigned long long cnt[NCAND], saved[NCAND];
static void hit(int c, long s) { cnt[c]++; saved[c] += s > 0 ? (unsigned long long)s : 0; }

static unsigned long long total_cy, sup, mix[256], mixcy[256], calls, leaves;
static unsigned long long cstack[4096]; static int cleaf[4096]; static int csp;

static void process(unsigned long long i)
{
    rec *r = at(i);
    unsigned o = op(i);
    if (o != OP_WAIT) total_cy += r->cy;        /* (idle time is not work) */
    sup += r->fl & 1; mix[o]++; mixcy[o] += r->cy;
    /* calls */
    if (o == OP_CALL || o == OP_CALLR) {
        calls++;
        if (csp) cleaf[csp - 1] = 0;
        if (csp < 4096) { cstack[csp] = r->cy; cleaf[csp] = 1; csp++; }
    } else if (o == OP_RET && csp) {
        csp--;
        if (cleaf[csp]) { leaves++; hit(C_LEAF, ((long)cstack[csp] - 3) + ((long)r->cy - 4)); }
    }
    if (!used[i % RING]) {
        /* 1. scaled: SLLI t,i,k ; ADD x,a,t ; mem 0(x) */
        if (o == OP_SLLI && seq(i) && op(i + 1) == OP_ADD && seq(i + 1)) {
            unsigned t = fA(i), k = imm(i) & 31, x = fA(i + 1);
            if ((t == fB(i + 1) || t == fC(i + 1)) && k >= 1 && k <= 3 && (x == t || dead_after(i + 1, t))
                && memop0(i + 2, x)) {
                hit(C_SCALED, at(i)->cy + at(i + 1)->cy);
                used[i % RING] = used[(i + 1) % RING] = used[(i + 2) % RING] = 1;
                goto compiler;
            }
        }
        /* 2. indexed: ADD x,a,b ; mem 0(x) */
        if (o == OP_ADD && fC(i) != 0 && fB(i) != 0 && seq(i) && !used[(i + 1) % RING] && memop0(i + 1, fA(i))) {
            hit(C_INDEXED, r->cy);
            used[i % RING] = used[(i + 1) % RING] = 1;
            goto compiler;
        }
        /* 3. add shifted: SLLI t,i,k ; ADD x,a,t (t dead) */
        if (o == OP_SLLI && seq(i) && op(i + 1) == OP_ADD && !used[(i + 1) % RING]) {
            unsigned t = fA(i), k = imm(i) & 31;
            if ((t == fB(i + 1) || t == fC(i + 1)) && k >= 1 && k <= 3 && (fA(i + 1) == t || dead_after(i + 1, t))) {
                hit(C_ADDSL, r->cy);
                used[i % RING] = used[(i + 1) % RING] = 1;
            }
        }
    }
compiler:
    if (o == OP_ADDI && seq(i) && memop0(i + 1, fA(i)))
        hit(C_FOLDADDI, r->cy);
    if (o == OP_LUI && seq(i) && op(i + 1) == OP_ORI && fA(i + 1) == fA(i) && fB(i + 1) == fA(i)) {
        hit(C_LI32, (long)at(i + 1)->cy - 1);
        if (seq(i + 1) && memop0(i + 2, fA(i)))
            hit(C_LAMEM, at(i + 1)->cy);
    }
    if ((isload(o) || isstore(o)) && seq(i) && op(i + 1) == OP_ADDI && fA(i + 1) == fB(i) && fB(i + 1) == fB(i)
        && fB(i) != 0 && !(isload(o) && fA(i) == fB(i)) && (int16_t)imm(i + 1) >= -8 && (int16_t)imm(i + 1) <= 8)
        hit(C_POSTINC, (long)at(i + 1)->cy - 1);
    if (o == OP_LW && seq(i) && op(i + 1) == OP_LW && fB(i) == fB(i + 1) && imm(i) == imm(i + 1) && fA(i) != fB(i))
        hit(C_RELOAD, at(i + 1)->cy);
    if ((o == OP_ADDI || o == OP_ORI) && fB(i) == 0 && fA(i) != 0 && seq(i) && isbranch(op(i + 1))) {
        unsigned x = fA(i);
        int v = o == OP_ORI ? (int)imm(i) : (int)(int16_t)imm(i);
        if ((x == fA(i + 1) || x == fB(i + 1)) && v >= -128 && v < 128 && dead_after(i + 1, x))
            hit(C_BRIMM, r->cy);
    }
    /* save / restore runs on sp or the frame pointer, counted at their first */
    if ((o == OP_SW || o == OP_LW) && (fB(i) == 14 || fB(i) == 15)) {
        int first = !(i > 0 && at(i - 1)->pc + 4 == r->pc && op(i - 1) == o && fB(i - 1) == fB(i)
                      && (o == OP_SW || fA(i - 1) != fB(i)));
        if (first) {
            unsigned long long j = i;
            long cy = r->cy;
            while (j + 1 < n && j + 1 < i + LOOK * 2 && seq(j) && op(j + 1) == o && fB(j + 1) == fB(i)
                   && (o == OP_SW || fA(j + 1) != fB(i))) { j++; cy += at(j)->cy; }
            long k = (long)(j - i + 1);
            if (k >= 2) hit(o == OP_SW ? C_STM : C_LDM, cy - (2 + 2 * k));
        }
    }
}

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : stdin;
    if (!f) { perror(argv[1]); return 1; }
    rec x;
    for (;;) {
        int got = fread(&x, sizeof x, 1, f) == 1;
        if (got) { *at(n) = x; used[n % RING] = 0; n++; }
        while (head < n && (!got || n - head > HORIZON + 2 * LOOK)) process(head++);
        if (!got) break;
    }
    printf("instructions %llu, cycles %llu without WAIT (%.2f per instruction), supervisor %.1f%%\n\n",
           n, total_cy, (double)total_cy / n, 100.0 * sup / n);
    printf("%-52s %11s %12s %7s\n", "candidate", "instances", "cycles saved", "of all");
    int ord[NCAND];
    for (int i = 0; i < NCAND; i++) ord[i] = i;
    for (int i = 0; i < NCAND; i++) for (int j = i + 1; j < NCAND; j++)
        if (saved[ord[j]] > saved[ord[i]]) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    for (int k = 0; k < NCAND; k++) { int c = ord[k];
        printf("%-52s %11llu %12llu %6.1f%%\n", cname[c], cnt[c], saved[c], 100.0 * saved[c] / total_cy); }
    printf("\ncalls %llu, leaf %llu (%.0f%%)\n\n%-8s %11s %6s %12s %6s %5s\n", calls, leaves,
           100.0 * leaves / (calls ? calls : 1), "op", "count", "%", "cycles", "%", "cpi");
    for (int k = 0; k < 28; k++) {
        int best = -1;
        for (int o = 0; o < 256; o++) if (mix[o] && (best < 0 || mix[o] > mix[best])) best = o;
        if (best < 0) break;
        printf("%-8s %11llu %5.1f%% %12llu %5.1f%% %5.2f\n", p32_name[best] ? p32_name[best] : "?", mix[best],
               100.0 * mix[best] / n, mixcy[best], 100.0 * mixcy[best] / total_cy, (double)mixcy[best] / mix[best]);
        mix[best] = 0;
    }
    return 0;
}
