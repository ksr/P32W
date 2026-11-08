/*
 * p32emu.c -- P32W instruction-level reference emulator (D7).
 *
 * Decodes each 32-bit instruction and performs its effect directly; there is
 * no microcode model yet. This is the golden model the later microcode-level
 * emulator, the FPGA ALU and the hardware are checked against.
 *
 * Opcodes, formats, must-be-zero masks, the privileged set, control registers
 * and exception causes come from p32opc.h, generated from microcode/p32isa.py
 * (the single source of truth). Semantics follow docs/isa-v0.md and
 * docs/isa-system.md (stage A: traps and privilege; stage B: interrupts;
 * stage C: virtual memory).
 *
 * v4 scope: user/supervisor modes with banked r15, traps through VBR,
 * interrupts (IRQ0 = timer) taken between instructions, WAIT, and a
 * software-managed split I/D TLB (64 + 64 entries, direct-mapped,
 * ASID-tagged; D13) that translates every user-mode access (supervisor mode
 * is untranslated). The timer ticks
 * once per instruction, so interrupt timing is exactly repeatable (D11).
 * While VBR = 0 (no handler installed) an exception stops the emulator with
 * a register dump, as in v1.
 *
 * Usage: p32emu [-t] [-m] [-k FILE] [-M CARDS] [-s FILE] [-l N] [-d IMG] [-a OUT] [-A IN] [-p N] image.bin
 *   -t    trace every instruction (and every trap) to stderr
 *   -m    TLB statistics at exit, with what-if larger or 2-way TLBs
 *   -M N  memory cards fitted, 1 (default) to 4, 16 MiB each, at the windows
 *         (or -M 16,6: each window's MiB, a card's fitted banks, D60)
 *         0 .. N-1 (D52): RAM is 1 MiB up to N x 16 MiB; above it a BUSERROR
 *   -k F  a kernel profile at exit to F: how many instructions ran at each
 *         supervisor-mode address in RAM's first MiB (the kernel), and in
 *         user mode, for tools/kprof.py to total by function
 *   -s F  write the architectural state at every instruction start to F
 *         (same format as p32uemu, for cross-checking)
 *   -l N  stop after N instructions (default 100000000, or no limit when
 *         the console is a terminal; 0 = no limit)
 *   -d, -a, -A, -p: the I/O card's CF image, UART 1 files, input port (p32io.h)
 * Exit status: r1 & 0xFF on HALT; 255 on an unhandled exception, the
 * instruction limit, or a usage/load error.
 */
#define _DEFAULT_SOURCE        /* (Linux: the POSIX and BSD calls a Mac gives anyway, with -std=c99; D96) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include "p32opc.h"
#include "p32io.h"                     /* the I/O card: UARTs, timer, ports, CF (D50) */
#include "p32dbg.h"                    /* -dbg: the debug server (D97) */

/* Memory map (isa-v0.md section 5; D50: the RAM actually fitted) */
#define ROM_SIZE   0x00100000u          /* 0x0000_0000 - 0x000F_FFFF */
#define RAM_BASE   0x00100000u          /* RAM from 1 MiB (the ROM hides the first) ... */
#define CARD_RAM   0x01000000u          /* ... in memory cards of 16 MiB (D50, D52) */
#define CAUSE_IRQ  0x80000000u
#define USER_TOP   0x80000000u          /* user space is the lower 2 GiB (C2) */

/* TLB (isa-system.md C3) */
#define TLB_N      64
#define PTE_V      1u
#define PTE_W      2u
#define PTE_X      4u
#define TLBHI_I    0x100u               /* I-TLB (fetch) vs D-TLB (D13) */
enum access { ACC_FETCH, ACC_LOAD, ACC_STORE };

/* STATUS bits (isa-system.md section 2) */
#define ST_S       1u                   /* supervisor */
#define ST_IE      2u                   /* interrupt enable (stage B) */
#define ST_PS      4u                   /* previous S */
#define ST_PIE     8u                   /* previous IE */
#define ST_MASK    15u

static uint32_t r[16];                  /* r15 = SP of the current mode */
static uint32_t sp_other;               /* the inactive r15 bank */
static uint32_t pc;
static uint32_t status = ST_S;          /* reset: supervisor, interrupts off */
static uint32_t epc, cause, badaddr, vbr, imask;
static uint32_t asid, tlbhi, tlblo;     /* MMU control registers */
static struct { uint32_t hi, lo; } tlb[2][TLB_N];  /* [0] D-TLB, [1] I-TLB;
                                           hi = VPN | ASID, lo = PFN | X W V */
static uint8_t *rom, *ram;
static uint32_t cur_pc, cur_ins;        /* instruction being executed */
static unsigned long long icount;
static int trace;
static FILE *strace;                    /* -s: architectural state per instruction */
static FILE *kprof;                     /* -k: the kernel profile */
#define KPROF_WORDS (0x00100000u / 4)   /* the kernel's MiB, 0x0010_0000 - 0x001F_FFFF */
static unsigned long long *kcount, kuser, kother;
static jmp_buf abort_ins;               /* a trap abandons the current instruction */

static void disasm(uint32_t pc_, uint32_t ins, char *buf, size_t n);

static void dump(void)
{
    char d[64];
    disasm(cur_pc, cur_ins, d, sizeof d);
    fprintf(stderr, "  pc=%08X  ins=%08X  %s   (%llu instructions)\n",
            cur_pc, cur_ins, d, icount);
    fprintf(stderr, "  mode=%s  status=%X  epc=%08X  cause=%08X  badaddr=%08X  vbr=%08X  %s=%08X\n",
            (status & ST_S) ? "S" : "U", status, epc, cause, badaddr, vbr,
            (status & ST_S) ? "usp" : "ssp", sp_other);
    fprintf(stderr, "  imask=%02X  timer: count=%u reload=%u enable=%d pending=%d  asid=%u tlbhi=%08X tlblo=%08X\n",
            imask, t_count, t_reload, t_enable, t_pending, asid, tlbhi, tlblo);
    for (int i = 0; i < 16; i++)
        fprintf(stderr, "  r%-2d=%08X%s", i, r[i], (i % 4 == 3) ? "\n" : "");
}

/* Switch mode; r15 follows (banked SP, D6). */
static void set_mode(int super)
{
    if (!!(status & ST_S) != !!super) {
        uint32_t t = r[15];
        r[15] = sp_other;
        sp_other = t;
    }
    status = super ? (status | ST_S) : (status & ~ST_S);
}

/* Trap entry, shared by exceptions and interrupts (isa-system.md section 5).
   Writes no memory (D10). */
static void enter_trap(uint32_t code, uint32_t addr, uint32_t at)
{
    epc = at;
    cause = code;
    badaddr = addr;
    uint32_t prev = status;
    status = (status & ~(ST_PS | ST_PIE)) | ((prev & ST_S) ? ST_PS : 0) | ((prev & ST_IE) ? ST_PIE : 0);
    set_mode(1);
    status &= ~ST_IE;
    pc = vbr & ~3u;                     /* PC holds bits 31:2 (D15) */
}

/*
 * Raise an exception. The current instruction has had no effect (every
 * check happens before any state is written), so EPC = its address and RTE
 * re-executes it.
 */
static void trap(unsigned code, uint32_t addr, const char *what)
{
    if (vbr == 0) {                     /* no handler installed: stop (D10) */
        fflush(stdout);
        fprintf(stderr, "EXCEPTION: %s at address %08X\n", what, addr);
        dump();
        exit(255);
    }
    if (trace)
        fprintf(stderr, "  TRAP %s (%u) addr=%08X -> %08X\n",
                p32_causename[code] ? p32_causename[code] : "?", code, addr, vbr);
    enter_trap(code, addr, cur_pc);
    longjmp(abort_ins, 1);
}

/* ---------------------------------------------------------------- interrupts */
/* Taken between instructions (B2): EPC = the next, unexecuted instruction. */
static void interrupt(uint32_t mask)
{
    uint32_t lines = io_ipend() & mask;
    unsigned n = 0;
    while (!(lines & (1u << n)))
        n++;                            /* fixed priority: lowest line first */
    if (vbr == 0) {
        fflush(stdout);
        fprintf(stderr, "EXCEPTION: interrupt IRQ%u with no handler (VBR = 0)\n", n);
        dump();
        exit(255);
    }
    if (trace)
        fprintf(stderr, "  INTERRUPT IRQ%u at %08X -> %08X\n", n, pc, vbr);
    enter_trap(CAUSE_IRQ | io_iack(n) << 8 | n, 0, pc);     /* the vector: D57 */
}

/* -M: the memory cards fitted (D50, D52), 16 MiB each, jumpered to the
   windows 0 .. N-1 (the card in window 0 has the ROM). An address in a window
   with no card gets no -ACK: a BUSERROR, which the kernel's probe takes */
static unsigned card_mib[4] = { 16 };    /* MiB fitted in each window (D60): 0 = no card */
static int ram_fitted(uint32_t a)
{
    unsigned w = a / CARD_RAM;
    return w < 4 && (a % CARD_RAM) < (card_mib[w] << 20);
}
static uint32_t ram_end(void)            /* the end of the highest card's window */
{
    unsigned w = 4;
    while (w > 1 && !card_mib[w - 1])
        w--;
    return w * CARD_RAM;
}
/* -M N (N full cards in windows 0 .. N-1) or -M S0,S1,... (each window's
   MiB, an even 2-16, 0 for no card; window 0 needs one): 1 if used, -1 bad, 0 not -M */
static int ram_option(int argc, char **argv, int i)
{
    if (strcmp(argv[i], "-M") || i + 1 >= argc) return 0;
    const char *s = argv[i + 1];
    unsigned v[4] = { 0 }, n = 0;
    if (!strchr(s, ',')) {
        int k = atoi(s);
        if (k < 1 || k > 4) { fprintf(stderr, "-M: 1 to 4 cards, or each window's MiB (16,6)\n"); return -1; }
        for (n = 0; n < (unsigned)k; n++) v[n] = 16;
    } else {
        for (; *s && n < 4; n++) {
            char *e;
            unsigned long m = strtoul(s, &e, 10);
            if (e == s || m > 16 || m % 2) { fprintf(stderr, "-M: each window's MiB: 0, 2, 4 ... 16\n"); return -1; }
            v[n] = (unsigned)m;
            s = *e == ',' ? e + 1 : e;
        }
        if (*s || !v[0]) { fprintf(stderr, "-M: at most 4 windows; window 0 needs a card\n"); return -1; }
    }
    memcpy(card_mib, v, sizeof v);
    return 1;
}

/* ---------------------------------------------------------------- memory */
/* Host pointer for an aligned access; traps if unmapped. */
static uint8_t *map(uint32_t a, int write)
{
    if (a < ROM_SIZE) {
        if (write)
            trap(EXC_BUSERROR, a, "write to ROM");
        return rom + a;
    }
    if (a >= RAM_BASE && ram_fitted(a))
        return ram + (a - RAM_BASE);
    if (vid_ram(a))
        return vid_ram(a);              /* the video card's RAM (D52) */
    trap(EXC_BUSERROR, a, write ? "store to unmapped address (no memory card answers)" : "load from unmapped address (no memory card answers)");
    return NULL;
}

/*
 * Virtual -> physical (stage C). Supervisor mode is untranslated. In user
 * mode: upper half -> PRIVILEGE; no valid entry with matching VPN and ASID ->
 * TLB miss; store to W = 0 or fetch from X = 0 -> PROTECT. TLBHI is set to
 * the faulting VPN and current ASID on a miss or protection fault (C4).
 */
/* -m: TLB statistics, printed at exit. Every user-mode access is counted
 * against the real split TLB (64 + 64 entries, direct-mapped, D12/D13), and
 * replayed into four "what if" shadow TLBs per side -- more entries, two
 * ways (LRU), or 64 entries indexed by a hash of the page number (VPN XOR
 * VPN >> 6: two 74HCT86s of logic) -- which assume every miss is
 * refilled. A TLBW of an invalid
 * entry (the kernel's flush) empties the matching slots of the shadows too. */
static int memstats;
static struct { unsigned long long acc, miss, cold, prot, writes, replaced; } tst[2];
#define NSHADOW 4
static const unsigned sh_sets[NSHADOW] = { 128, 32, 64, 64 }, sh_ways[NSHADOW] = { 1, 2, 2, 1 };
static const int sh_hash[NSHADOW] = { 0, 0, 0, 1 };
static uint32_t shtlb[NSHADOW][2][128 * 2];    /* (VPN << 8 | ASID) + 1; 0 empty; each set MRU first */
static unsigned long long sh_miss[NSHADOW][2];

static void shadow_access(int side, uint32_t va)
{
    uint32_t key = ((va >> 12) << 8 | asid) + 1;
    for (int s = 0; s < NSHADOW; s++) {
        uint32_t vpn = va >> 12;
        unsigned ways = sh_ways[s], set = (sh_hash[s] ? vpn ^ vpn >> 6 : vpn) % sh_sets[s], w;
        uint32_t *e = &shtlb[s][side][set * ways];
        for (w = 0; w < ways && e[w] != key; w++)
            ;
        if (w == ways) {
            sh_miss[s][side]++;
            w = ways - 1;               /* (the least recently used goes) */
        }
        for (; w > 0; w--)
            e[w] = e[w - 1];
        e[0] = key;
    }
}

static void shadow_flush(int side, unsigned idx)    /* the slots that real slot idx covers */
{
    for (int s = 0; s < NSHADOW; s++)
        for (unsigned k = 0; k < sh_sets[s] * sh_ways[s]; k++)
            if (shtlb[s][side][k] && (((shtlb[s][side][k] - 1) >> 8) & (TLB_N - 1)) == idx)    /* (by VPN) */
                shtlb[s][side][k] = 0;
}

static void pct(unsigned long long n, unsigned long long of)
{
    unsigned long long t = of ? n * 10000 / of : 0;
    fprintf(stderr, "%3llu.%02llu%%", t / 100, t % 100);
}

static void print_memstats(void)
{
    static const char *const side_name[2] = { "D-TLB", "I-TLB" };
    fprintf(stderr, "\nTLB statistics (user-mode accesses; -m):\n");
    fprintf(stderr, "         accesses       misses  (cold  conflict)  miss rate  protect   TLBW  replaced\n");
    for (int side = 1; side >= 0; side--) {
        fprintf(stderr, "%s %12llu %12llu %7llu %9llu    ", side_name[side], tst[side].acc, tst[side].miss,
                tst[side].cold, tst[side].miss - tst[side].cold);
        pct(tst[side].miss, tst[side].acc);
        fprintf(stderr, " %8llu %6llu %9llu\n", tst[side].prot, tst[side].writes, tst[side].replaced);
    }
    fprintf(stderr, "what if (the same accesses, every miss refilled):\n");
    for (int s = 0; s < NSHADOW; s++) {
        fprintf(stderr, "  %3u entries, %u-way%s:  I-TLB %10llu misses ", sh_sets[s] * sh_ways[s], sh_ways[s],
                sh_hash[s] ? ", hashed" : "        ", sh_miss[s][1]);
        pct(sh_miss[s][1], tst[1].acc);
        fprintf(stderr, "   D-TLB %10llu misses ", sh_miss[s][0]);
        pct(sh_miss[s][0], tst[0].acc);
        fputc('\n', stderr);
    }
}

static uint32_t translate(uint32_t va, enum access acc)
{
    if (status & ST_S)
        return va;
    if (va >= USER_TOP)
        trap(EXC_PRIVILEGE, va, "user access to the upper 2 GiB");
    int side = acc == ACC_FETCH;        /* fetches use the I-TLB (D13) */
    unsigned idx = (va >> 12) & (TLB_N - 1);
    uint32_t hi = tlb[side][idx].hi, lo = tlb[side][idx].lo;
    uint32_t fault_hi = (va & ~0xFFFu) | (side ? TLBHI_I : 0) | asid;
    if (memstats) {
        tst[side].acc++;
        shadow_access(side, va);
    }
    if (!(lo & PTE_V) || (hi >> 12) != (va >> 12) || (hi & 0xFF) != asid) {
        if (memstats) {
            tst[side].miss++;
            if (!(lo & PTE_V)) tst[side].cold++;
        }
        tlbhi = fault_hi;
        static const unsigned code[] = { EXC_TLBMISS_X, EXC_TLBMISS_R, EXC_TLBMISS_W };
        static const char *const what[] = { "TLB miss on fetch", "TLB miss on load", "TLB miss on store" };
        trap(code[acc], va, what[acc]);
    }
    if ((acc == ACC_STORE && !(lo & PTE_W)) || (acc == ACC_FETCH && !(lo & PTE_X))) {
        if (memstats) tst[side].prot++;
        tlbhi = fault_hi;
        trap(EXC_PROTECT, va, acc == ACC_STORE ? "store to a read-only page" : "fetch from a non-executable page");
    }
    return (lo & ~0xFFFu) | (va & 0xFFFu);
}

/* size = 1, 2 or 4 bytes; little-endian; misaligned access traps (D4).
   Check order (C5): alignment, upper half, TLB, physical bus. */
static uint32_t load_acc(uint32_t a, int size, enum access acc)
{
    if (a & (uint32_t)(size - 1))
        trap(EXC_MISALIGNED, a, "misaligned load");
    uint32_t pa = translate(a, acc);
    if (pa >= IO_BASE) {
        uint32_t v;
        if (io_read(pa, size, &v))
            trap(EXC_BUSERROR, pa, "no device answers at this I/O address (or wrong access size)");
        return v;
    }
    uint8_t *p = map(pa, 0);
    uint32_t v = 0;
    for (int i = size - 1; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}

static uint32_t load(uint32_t a, int size)
{
    return load_acc(a, size, ACC_LOAD);
}

static void store(uint32_t a, int size, uint32_t v)
{
    if (a & (uint32_t)(size - 1))
        trap(EXC_MISALIGNED, a, "misaligned store");
    uint32_t pa = translate(a, ACC_STORE);
    if (pa >= IO_BASE) {
        if (io_write(pa, size, v))
            trap(EXC_BUSERROR, pa, "no device answers at this I/O address (or wrong access size)");
        return;
    }
    uint8_t *p = map(pa, 1);
    for (int i = 0; i < size; i++, v >>= 8)
        p[i] = (uint8_t)v;
}

/* ---------------------------------------------------------------- control registers */
static uint32_t cr_read(unsigned n)
{
    switch (n) {
    case CR_STATUS:  return status;
    case CR_EPC:     return epc;
    case CR_CAUSE:   return cause;
    case CR_BADADDR: return badaddr;
    case CR_VBR:     return vbr;
    case CR_USP:     return sp_other;   /* MFCR is privileged: r15 is the SSP */
    case CR_IPEND:   return io_ipend();
    case CR_IMASK:   return imask;
    case CR_ASID:    return asid;
    case CR_TLBHI:   return tlbhi;
    case CR_TLBLO:   return tlblo;
    }
    trap(EXC_ILLEGAL, 0, "reserved control register");
    return 0;
}

static void cr_write(unsigned n, uint32_t v)
{
    switch (n) {
    case CR_STATUS:
        set_mode((v & ST_S) != 0);      /* takes effect at once */
        status = (status & ST_S) | (v & ST_MASK & ~ST_S);
        return;
    case CR_EPC:     epc = v; return;
    case CR_CAUSE:   cause = v; return;
    case CR_BADADDR: badaddr = v; return;
    case CR_VBR:     vbr = v; return;   /* low bits ignored on use (D15) */
    case CR_USP:     sp_other = v; return;
    case CR_IPEND:   return;            /* read-only */
    case CR_IMASK:   imask = v & 0xFF; return;
    case CR_ASID:    asid = v & 0xFF; return;
    case CR_TLBHI:   tlbhi = v & 0xFFFFF1FFu; return;
    case CR_TLBLO:   tlblo = v & 0xFFFFF007u; return;
    }
    trap(EXC_ILLEGAL, 0, "reserved control register");
}

/* ---------------------------------------------------------------- disassembler */
static void disasm(uint32_t pc_, uint32_t ins, char *buf, size_t n)
{
    unsigned op = ins >> 24, A = (ins >> 20) & 15, B = (ins >> 16) & 15, C = (ins >> 12) & 15;
    int32_t simm = (int16_t)(ins & 0xFFFF);
    int32_t off24 = (int32_t)(ins << 8) >> 8;
    const char *m = p32_name[op];
    const char *cr = p32_crname[B] ? p32_crname[B] : "cr?";
    if (!m || (ins & p32_zmask[op])) {
        snprintf(buf, n, "(illegal)");
        return;
    }
    switch (p32_fmt[op]) {
    case F_R:   snprintf(buf, n, "%s r%u,r%u,r%u", m, A, B, C); break;
    case F_I:   snprintf(buf, n, "%s r%u,r%u,%d", m, A, B, simm); break;
    case F_IU:  snprintf(buf, n, "%s r%u,r%u,0x%X", m, A, B, ins & 0xFFFF); break;
    case F_SH:  snprintf(buf, n, "%s r%u,r%u,%u", m, A, B, ins & 31); break;
    case F_LUI: snprintf(buf, n, "%s r%u,0x%X", m, A, ins & 0xFFFF); break;
    case F_LD:
    case F_ST:  snprintf(buf, n, "%s r%u,%d(r%u)", m, A, simm, B); break;
    case F_BR:  snprintf(buf, n, "%s r%u,r%u,%08X", m, A, B, pc_ + 4 + (uint32_t)simm * 4); break;
    case F_J:   snprintf(buf, n, "%s %08X", m, pc_ + 4 + (uint32_t)off24 * 4); break;
    case F_JR:  snprintf(buf, n, "%s r%u", m, B); break;
    case F_CR:
        if (op == OP_MFCR) snprintf(buf, n, "%s r%u,%s", m, A, cr);
        else               snprintf(buf, n, "%s %s,r%u", m, cr, A);
        break;
    case F_X:   snprintf(buf, n, "%s r%u,r%u,r%u,%u", m, A, B, C, 1u << (ins & 3)); break;
    case F_P:   snprintf(buf, n, "%s r%u,%d(r%u),%d", m, A, (int8_t)(ins >> 8), B, (int8_t)ins); break;
    case F_L32: snprintf(buf, n, "%s r%u,(the next word)", m, B); break;
    default:    snprintf(buf, n, "%s", m); break;
    }
}

/* ---------------------------------------------------------------- execution */
static int shadow;                      /* the last instruction was MTCR or RTE (D20) */
static uint32_t sh_ie, sh_im;           /* IE, IMASK before it */
/* -dbg (p32dbg.h): the machine as the debug server sees it */
static uint32_t dbg_status_pc(void) { return pc; }
static int dbg_supervisor(void) { return (status & ST_S) != 0; }
static unsigned long long dbg_instrs(void) { return icount; }
static unsigned long long dbg_cycles(void) { return icount; }   /* (no microcycles here) */
static int dbg_ustate(struct sbuf *s) { (void)s; return 0; }

static void dbg_regs(struct sbuf *s)
{
    sb_add(s, "\"r\":[");
    for (int i = 0; i < 16; i++) sb_add(s, "%s%u", i ? "," : "", r[i]);
    sb_add(s, "],\"sp_other\":%u,\"status\":%u,\"imask\":%u,\"epc\":%u,\"cause\":%u,\"badaddr\":%u,\"vbr\":%u,"
           "\"asid\":%u,\"tlbhi\":%u,\"tlblo\":%u,\"timer\":[%u,%u,%d,%d],",
           sp_other, status, imask, epc, cause, badaddr, vbr, asid, tlbhi, tlblo, t_count, t_reload, t_enable, t_pending);
}

static int dbg_set(const char *name, uint32_t v)
{
    static const char *const cr[] = { "pc", "sp_other", "status", "imask", "epc", "cause", "badaddr", "vbr", "asid" };
    uint32_t *const crv[] = { &pc, &sp_other, &status, &imask, &epc, &cause, &badaddr, &vbr, &asid };
    char *e;
    if (name[0] == 'r' && name[1]) {
        long n = strtol(name + 1, &e, 10);
        if (!*e && n >= 1 && n <= 15) { r[n] = v; return 1; }
        return 0;                       /* (r0 is zero) */
    }
    if (!strcmp(name, "sp")) { r[15] = v; return 1; }
    if (!strcmp(name, "status")) { set_mode((v & ST_S) != 0); status = v; return 1; }   /* (r15's banks follow S) */
    for (unsigned k = 0; k < sizeof cr / sizeof cr[0]; k++)
        if (!strcmp(name, cr[k])) { *crv[k] = k == 0 ? v & ~3u : v; return 1; }
    return 0;
}

/* the physical address of a, as a load would see it (no fault, no counts); 0: not mapped */
static int dbg_phys(uint32_t a, int phys, uint32_t *pa)
{
    if (phys || (status & ST_S)) { *pa = a; return 1; }
    if (a >= USER_TOP) return 0;
    unsigned idx = (a >> 12) & (TLB_N - 1);
    for (int side = 0; side < 2; side++) {      /* the D-TLB, then the I-TLB (code) */
        uint32_t hi = tlb[side][idx].hi, lo = tlb[side][idx].lo;
        if ((lo & PTE_V) && (hi >> 12) == (a >> 12) && (hi & 0xFF) == asid) {
            *pa = (lo & ~0xFFFu) | (a & 0xFFFu);
            return 1;
        }
    }
    return 0;
}

static uint8_t *dbg_byte(uint32_t pa)          /* memory only: never a device register */
{
    if (pa < ROM_SIZE) return rom + pa;
    if (pa >= RAM_BASE && ram_fitted(pa)) return ram + (pa - RAM_BASE);
    return vid_ram(pa);
}

static int dbg_read(uint32_t a, int phys, uint8_t *v)
{
    uint32_t pa;
    uint8_t *p;
    if (!dbg_phys(a, phys, &pa) || !(p = dbg_byte(pa))) return 0;
    *v = *p;
    return 1;
}

static int dbg_write(uint32_t a, int phys, uint8_t v)
{
    uint32_t pa;
    uint8_t *p;
    if (!dbg_phys(a, phys, &pa) || !(p = dbg_byte(pa))) return 0;
    *p = v;
    return 1;
}

static void dbg_cpu_reset(void)                 /* as -RES (p32uemu: the RESET sequence): the register */
{                                               /* file keeps its contents; supervisor mode, IE off */
    set_mode(1);
    memset(tlb, 0, sizeof tlb);
    pc = imask = asid = tlbhi = tlblo = 0;
    status = ST_S;
    shadow = 0;
    t_enable = t_pending = 0;
}

static int slow;                        /* -s or -dbg (set before run) */

static void run(unsigned long long limit)
{
    setjmp(abort_ins);                  /* a trap resumes here, with pc = VBR */
    for (;;) {
        if (limit && icount >= limit) {
            fflush(stdout);
            fprintf(stderr, "STOP: instruction limit (%llu) reached\n", limit);
            dump();
            exit(255);
        }
        /* between instructions (B2), with the interrupt shadow (D20): after
           MTCR or RTE the check uses IE and IMASK from before that
           instruction, as the hardware decides in its last microcycle */
        uint32_t ie = status & ST_IE, im = imask;
        if (shadow) {
            ie = sh_ie;
            im = sh_im;
            shadow = 0;
        }
        if (ie && (io_ipend() & im))
            interrupt(im);
        sh_ie = status & ST_IE;
        sh_im = imask;
        if (slow) {                     /* -s or -dbg: one test between instructions for both */
            if (dbg_on)
                dbg_boundary(pc);
            if (strace)
                fprintf(strace, "%08X %c %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X o=%08X st=%X epc=%08X cause=%08X bad=%08X\n",
                    pc, (status & ST_S) ? 'S' : 'U', r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8],
                    r[9], r[10], r[11], r[12], r[13], r[14], r[15], sp_other, status, epc, cause, badaddr);
        }
        cur_pc = pc;
        cur_ins = 0;
        icount++;
        if (kcount) {
            if (!(status & ST_S)) kuser++;
            else if (pc - RAM_BASE < 0x00100000u) kcount[(pc - RAM_BASE) >> 2]++;
            else kother++;
        }
        io_tick();
        uint32_t ins = load_acc(pc, 4, ACC_FETCH);  /* PC is always word-aligned */
        cur_ins = ins;

        unsigned op = ins >> 24, A = (ins >> 20) & 15, B = (ins >> 16) & 15, C = (ins >> 12) & 15;
        uint32_t imm = ins & 0xFFFF;
        uint32_t simm = (uint32_t)(int32_t)(int16_t)imm;
        uint32_t off24 = (uint32_t)((int32_t)(ins << 8) >> 8);
        uint32_t a = r[B], b = r[C];
        uint32_t next = pc + 4;

        if (trace) {
            char d[64];
            disasm(cur_pc, ins, d, sizeof d);
            fprintf(stderr, "%08X %c %08X  %s\n", cur_pc, (status & ST_S) ? 'S' : 'U', ins, d);
        }
        if (p32_fmt[op] == F_ILLEGAL || (ins & p32_zmask[op]))
            trap(EXC_ILLEGAL, 0, "illegal instruction");
        if (p32_priv[op] && !(status & ST_S))
            trap(EXC_PRIVILEGE, 0, "privileged instruction in user mode");

        switch (op) {
        case OP_HALT:
            if (dbg_on) {
                dbg_halt("halt");
                continue;
            }
            fflush(stdout);
            if (idle_ticks)
                fprintf(stderr, "HALT at %08X, r1=%08X (%llu instructions, %llu idle ticks)\n",
                        cur_pc, r[1], icount, idle_ticks);
            else
                fprintf(stderr, "HALT at %08X, r1=%08X (%llu instructions)\n", cur_pc, r[1], icount);
            exit((int)(r[1] & 0xFF));

        /* register-register ALU */
        case OP_ADD:  r[A] = a + b; break;
        case OP_SUB:  r[A] = a - b; break;
        case OP_AND:  r[A] = a & b; break;
        case OP_OR:   r[A] = a | b; break;
        case OP_XOR:  r[A] = a ^ b; break;
        case OP_NOR:  r[A] = ~(a | b); break;
        case OP_SLL:  r[A] = a << (b & 31); break;
        case OP_SRL:  r[A] = a >> (b & 31); break;
        case OP_SRA:  r[A] = (uint32_t)((int32_t)a >> (b & 31)); break;
        case OP_SLT:  r[A] = (int32_t)a < (int32_t)b; break;
        case OP_SLTU: r[A] = a < b; break;

        /* immediate ALU */
        case OP_ADDI:  r[A] = a + simm; break;
        case OP_ANDI:  r[A] = a & imm; break;
        case OP_ORI:   r[A] = a | imm; break;
        case OP_XORI:  r[A] = a ^ imm; break;
        case OP_SLLI:  r[A] = a << (imm & 31); break;
        case OP_SRLI:  r[A] = a >> (imm & 31); break;
        case OP_SRAI:  r[A] = (uint32_t)((int32_t)a >> (imm & 31)); break;
        case OP_SLTI:  r[A] = (int32_t)a < (int32_t)simm; break;
        case OP_SLTIU: r[A] = a < simm; break;
        case OP_LUI:   r[A] = imm << 16; break;

        /* multiply / divide -- divide never traps (isa-v0.md) */
        case OP_MUL:   r[A] = (uint32_t)((uint64_t)a * b); break;
        case OP_MULH:  r[A] = (uint32_t)((uint64_t)((int64_t)(int32_t)a * (int32_t)b) >> 32); break;
        case OP_MULHU: r[A] = (uint32_t)(((uint64_t)a * b) >> 32); break;
        case OP_DIV:
            if (b == 0)                                  r[A] = 0xFFFFFFFFu;
            else if (a == 0x80000000u && b == 0xFFFFFFFFu) r[A] = 0x80000000u;
            else                                         r[A] = (uint32_t)((int32_t)a / (int32_t)b);
            break;
        case OP_DIVU:  r[A] = b ? a / b : 0xFFFFFFFFu; break;
        case OP_REM:
            if (b == 0)                                  r[A] = a;
            else if (a == 0x80000000u && b == 0xFFFFFFFFu) r[A] = 0;
            else                                         r[A] = (uint32_t)((int32_t)a % (int32_t)b);
            break;
        case OP_REMU:  r[A] = b ? a % b : a; break;

        /* loads and stores: address = rs1 + sext(imm); a trap leaves rd unchanged */
        case OP_LB:  r[A] = (uint32_t)(int32_t)(int8_t)load(a + simm, 1); break;
        case OP_LH:  r[A] = (uint32_t)(int32_t)(int16_t)load(a + simm, 2); break;
        case OP_LW:  r[A] = load(a + simm, 4); break;
        case OP_LBU: r[A] = load(a + simm, 1); break;
        case OP_LHU: r[A] = load(a + simm, 2); break;
        case OP_SB:  store(a + simm, 1, r[A]); break;
        case OP_SH:  store(a + simm, 2, r[A]); break;
        case OP_SW:  store(a + simm, 4, r[A]); break;

        /* D59: indexed, address = rs + rt * scale */
        case OP_LBX:  r[A] = (uint32_t)(int32_t)(int8_t)load(a + (b << (ins & 3)), 1); break;
        case OP_LHX:  r[A] = (uint32_t)(int32_t)(int16_t)load(a + (b << (ins & 3)), 2); break;
        case OP_LWX:  r[A] = load(a + (b << (ins & 3)), 4); break;
        case OP_LBUX: r[A] = load(a + (b << (ins & 3)), 1); break;
        case OP_LHUX: r[A] = load(a + (b << (ins & 3)), 2); break;
        case OP_SBX:  store(a + (b << (ins & 3)), 1, r[A]); break;
        case OP_SHX:  store(a + (b << (ins & 3)), 2, r[A]); break;
        case OP_SWX:  store(a + (b << (ins & 3)), 4, r[A]); break;
        /* D59: post-increment, the access at rs + off8, then rs += inc8 (rs is
           written last: with rd = rs a load's value is overwritten) */
        case OP_LBP: case OP_LHP: case OP_LWP: case OP_LBUP: case OP_LHUP:
        case OP_SBP: case OP_SHP: case OP_SWP: {
            uint32_t ad = a + (uint32_t)(int32_t)(int8_t)(ins >> 8);
            int size = (op & 3) == 0 ? 1 : (op & 3) == 1 ? 2 : 4;
            if (op & 8)
                store(ad, size, r[A]);
            else {
                uint32_t v = load(ad, size);
                if (op == OP_LBP) v = (uint32_t)(int32_t)(int8_t)v;
                if (op == OP_LHP) v = (uint32_t)(int32_t)(int16_t)v;
                r[A] = v;
            }
            r[B] = a + (uint32_t)(int32_t)(int8_t)ins;
            break;
        }
        /* D59: the next word is the value (fetched as an instruction is) */
        case OP_LI32:
            r[B] = load_acc(pc + 4, 4, ACC_FETCH);
            next = pc + 8;
            break;

        case OP_SWAP: {                         /* D51: as the microcode, the load then the store */
            uint32_t old = load(a + simm, 4);
            store(a + simm, 4, r[A]);
            r[A] = old;
            break;
        }

        /* conditional branches: compare A with B (D5) */
        case OP_BEQ:  if (r[A] == a) next += simm * 4; break;
        case OP_BNE:  if (r[A] != a) next += simm * 4; break;
        case OP_BLT:  if ((int32_t)r[A] <  (int32_t)a) next += simm * 4; break;
        case OP_BGE:  if ((int32_t)r[A] >= (int32_t)a) next += simm * 4; break;
        case OP_BLTU: if (r[A] <  a) next += simm * 4; break;
        case OP_BGEU: if (r[A] >= a) next += simm * 4; break;

        /* jumps and calls; the stack is full-descending, SP = r15 (D6).
           SP and PC are committed only after the memory access succeeds. */
        case OP_JMP:
            next += off24 * 4;
            break;
        case OP_CALL: {
            uint32_t sp = r[15] - 4;
            store(sp, 4, next);
            r[15] = sp;
            next += off24 * 4;
            break;
        }
        case OP_JR:
            if (a & 3)
                trap(EXC_MISALIGNED, a, "misaligned jump target");
            next = a;
            break;
        case OP_CALLR: {
            if (a & 3)
                trap(EXC_MISALIGNED, a, "misaligned jump target");
            uint32_t sp = r[15] - 4;
            store(sp, 4, next);
            r[15] = sp;
            next = a;
            break;
        }
        case OP_RET: {
            uint32_t ra = load(r[15], 4);
            if (ra & 3)
                trap(EXC_MISALIGNED, ra, "misaligned return address");
            r[15] += 4;
            next = ra;
            break;
        }

        /* stage A: traps and privilege (isa-system.md) */
        case OP_SYSCALL: trap(EXC_SYSCALL, 0, "SYSCALL"); break;
        case OP_BREAK:   trap(EXC_BREAK, 0, "BREAK"); break;
        case OP_RTE:
            shadow = 1;
            next = epc & ~3u;           /* PC holds bits 31:2 (D15) */
            set_mode((status & ST_PS) != 0);
            status = (status & ~ST_IE) | ((status & ST_PIE) ? ST_IE : 0);
            break;
        case OP_WAIT:                   /* B3: idle until an enabled line is asserted */
            if (io_wait(imask)) {
                if (dbg_on) {
                    dbg_halt("wait: nothing can end it");
                    continue;
                }
                fflush(stdout);
                fprintf(stderr, "STOP: WAIT with no possible wake-up\n");
                dump();
                exit(255);
            }
            break;
        case OP_TLBW: {                 /* C4: index = VPN bits 5:0, half = TLBHI.I */
            int side = (tlbhi & TLBHI_I) != 0;
            unsigned idx = (tlbhi >> 12) & (TLB_N - 1);
            if (memstats) {
                tst[side].writes++;
                if ((tlb[side][idx].lo & PTE_V) && (tlblo & PTE_V)) tst[side].replaced++;
                if (!(tlblo & PTE_V)) shadow_flush(side, idx);
            }
            tlb[side][idx].hi = tlbhi & ~TLBHI_I;
            tlb[side][idx].lo = tlblo;
            if (trace)
                fprintf(stderr, "  TLBW %c[%u] VPN=%05X ASID=%u -> PFN=%05X %c%c%c\n",
                        side ? 'I' : 'D', idx, tlbhi >> 12, tlbhi & 0xFF, tlblo >> 12,
                        (tlblo & PTE_V) ? 'V' : '-', (tlblo & PTE_W) ? 'W' : '-',
                        (tlblo & PTE_X) ? 'X' : '-');
            break;
        }
        case OP_MFCR: r[A] = cr_read(B); break;
        case OP_MTCR: cr_write(B, r[A]); shadow = 1; break;

        default:
            trap(EXC_ILLEGAL, 0, "illegal instruction (no semantics)");
        }
        pc = next;                      /* commit: the instruction completed */
        r[0] = 0;
    }
}

/* -k: "user N", "other N" (supervisor outside the kernel's MiB: the ROM),
   then "ADDRESS N" for each kernel address that ran */
static void print_kprof(void)
{
    fprintf(kprof, "user %llu\nother %llu\n", kuser, kother);
    for (uint32_t k = 0; k < KPROF_WORDS; k++)
        if (kcount[k])
            fprintf(kprof, "%08X %llu\n", RAM_BASE + 4 * k, kcount[k]);
    fclose(kprof);
}

/* the disk card's DMA engine (D75): physical memory as the bus answers it
   (a write to the ROM, or nothing there: no -ACK) */
static uint8_t *dma_phys(uint32_t a, int write)
{
    if (a < ROM_SIZE)
        return write ? NULL : rom + a;
    if (a >= RAM_BASE && ram_fitted(a))
        return ram + (a - RAM_BASE);
    return vid_ram(a);
}

int main(int argc, char **argv)
{
    unsigned long long limit = 100000000ULL;
    int limit_set = 0;
    const char *image = NULL;
    int used;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t"))
            trace = 1;
        else if (!strcmp(argv[i], "-m"))
            memstats = 1;
        else if ((used = ram_option(argc, argv, i)) != 0) {
            if (used < 0)
                return 255;
            i++;
        } else if (!strcmp(argv[i], "-k") && i + 1 < argc) {
            if (!(kprof = fopen(argv[++i], "w")) || !(kcount = calloc(KPROF_WORDS, sizeof *kcount))) {
                perror(argv[i]);
                return 255;
            }
        }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            if (!(strace = fopen(argv[++i], "w"))) {
                perror(argv[i]);
                return 255;
            }
        } else if (!strcmp(argv[i], "-l") && i + 1 < argc)
            limit = strtoull(argv[++i], NULL, 0), limit_set = 1;
        else if ((used = io_option(argc, argv, i)) != 0) {
            if (used < 0)
                return 255;
            i += used - 1;
        } else if (argv[i][0] != '-' && !image)
            image = argv[i];
        else
            image = NULL, i = argc;     /* usage */
    }
    if (!image) {
        fprintf(stderr, "usage: p32emu [-t] [-m] [-k FILE] [-M CARDS] [-s FILE] [-l N] [-d IMG] [-a OUT] [-A IN] [-p N] image.bin\n" IO_USAGE);
        return 255;
    }
    io_init(trace);
    dma_map = dma_phys;
    if (memstats)
        atexit(print_memstats);
    if (kprof)
        atexit(print_kprof);         /* (every way out: HALT, a stop, the limit) */
    if (tty_raw && !limit_set)
        limit = 0;                      /* interactive: run until stopped (^], halt) */
    rom = calloc(ROM_SIZE, 1);
    ram = calloc(ram_end() - RAM_BASE, 1);
    if (!rom || !ram) {
        fprintf(stderr, "p32emu: out of memory\n");
        return 255;
    }
    FILE *f = fopen(image, "rb");
    if (!f) {
        perror(image);
        return 255;
    }
    size_t n = fread(rom, 1, ROM_SIZE, f);
    if (fgetc(f) != EOF) {
        fprintf(stderr, "p32emu: %s is larger than the 1 MiB ROM\n", image);
        return 255;
    }
    fclose(f);
    (void)n;
    pc = 0;                             /* reset: PC = 0, supervisor mode */
    if (dbg_port >= 0) {                /* -dbg: the front end drives it, without a limit unless -l */
        dbg_name = "p32emu";
        dbg_start();
        if (!limit_set) limit = 0;
    }
    slow = strace != NULL || dbg_on;
    run(limit);
    return 0;
}
