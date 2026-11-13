/*
 * p32uemu.c -- P32W microcode-level emulator (docs/microarch.md, D14, D56).
 *
 * Runs the real control-store image built by microcode/p32ucode.py, one
 * microcycle at a time, over a model of the CPU card and the MMU / data card:
 *   sequencer: microaddress {SEQ, OPCODE, STEP, COND}, IR, immediate unit,
 *             STATUS, IMASK, interrupt priority, cause/fault latches,
 *             CR decode table
 *   registers: 32 x 32 register file, two read ports and one write port
 *             (r15 bank rule; writes to r0 go to the write sink, physical
 *             28), PC counter (bits 31:2), MDR (the store data, replicated)
 *   ALU:      operand latches A, B and the function; the result; compare
 *             outputs EQ/LT/LTU of the latched operands
 *   MMU / data: MAR, split I/D TLB, ASID/TLBHI/TLBLO, alignment and upper-half
 *             checks, load steering
 *   memory, devices: ROM, RAM, the devices of p32io.h (as in p32emu)
 * Three buses (docs/microarch.md): A and B, the operands (register-file ports
 * A and B, the PC, the immediate unit), and R, the result, each with one
 * source a cycle; R goes to one destination at the clock edge, MDR may load
 * from B, MAR from the PC; the ALU latches A, B and its function, and its
 * result is on R from the next cycle. A fault in a memory cycle
 * suppresses that cycle's effects and enters the trap sequence.
 *
 * It knows no instruction semantics: those live only in the microcode. It is
 * checked against the instruction-level reference emulator p32emu (D7) by
 * comparing their -s state traces instruction by instruction.
 *
 * Usage: p32uemu [-u ucode.bin] [-s FILE] [-l N] [-c] [-C [-F]] [-G [-q FILE]] [-W N] [-D ADDR:PERIOD] [-M CARDS] image.bin
 *   -u F  control-store image (default ../microcode/p32ucode.bin)
 *   -s F  architectural state at every instruction start (as p32emu -s)
 *   -I F  an instruction trace: per instruction its PC, IR, microcycles
 *         (from its fetch to the next's; trap and interrupt entry count with
 *         the instruction before) and flags (bit 0 supervisor), four
 *         little-endian 32-bit words (for measuring ISA changes)
 *   -l N  stop after N instructions (default 100000000, or no limit when
 *         the console is a terminal; 0 = no limit)
 *   -M N  memory cards fitted, 1 (default) to 4, 16 MiB each, at the windows
 *         (or -M 16,6: each window's MiB, a card's fitted banks, D60)
 *         0 .. N-1 (D52): RAM is 1 MiB up to N x 16 MiB; above it a BUSERROR
 *   -c    timer ticks once per microcycle (hardware timing) instead of once
 *         per instruction (the reference emulator's timing, D11)
 *   -C    the I/O card's timer exactly: a tick every third microcycle (1 MHz)
 *   -F    with -C: the CPU card's clock jumper fitted (D74), 4 MHz: a tick
 *         every fourth microcycle, 1 MHz still
 *   -G    the measuring gate (D85), which implies -C: while bit 7 of the
 *         output port is up it counts; when the bit falls it reports on
 *         stderr the cycles and the time they take at the clock, the
 *         instructions, the idle cycles, the disk's sectors and commands and
 *         the console's bytes. P32W's cycles(1) raises and lowers the bit
 *   -q F  with -G, a profile of what runs while the gate is up, written at
 *         exit: the cycles at each address, in user mode and in the kernel,
 *         for tools/uprof.py to total by function (D88, D92)
 *   -W N  N wait states on every bus cycle (a fetch, load or store): the
 *         bus's -WAIT (D50), counted in cycles and timer ticks only -- the
 *         sequencer holds the microcycle, so nothing else changes
 *   -D ADDR:PERIOD  a DMA master on the bus (D50), to test the bus lock
 *         (D51): every PERIOD microcycles it asks for the bus; the arbiter
 *         grants it at the next microcycle boundary unless -LOCK is held,
 *         and it adds 1 to the RAM word at physical ADDR (a read and a
 *         write: two bus clocks, which a CPU bus cycle waits for). At HALT
 *         it reports how many it added and the word's value. -Dnolock
 *         ADDR:PERIOD: the arbiter ignores -LOCK (a swap can then lose one)
 * Exit status: r1 & 0xFF on HALT; 255 on an unhandled exception, the
 * instruction limit, or a usage/load error.
 */
#define _DEFAULT_SOURCE        /* (Linux: the POSIX and BSD calls a Mac gives anyway, with -std=c99; D96) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "p32opc.h"
#include "p32uc.h"
#include "p32io.h"                     /* the I/O card, shared with p32emu (D50) */
#define DBG_MICRO 1
#include "p32dbg.h"                    /* -dbg: the debug server (D97), with ustep and ustate */

#define ROM_SIZE   0x00100000u          /* the memory card's 1 MiB ROM ... */
#define RAM_BASE   0x00100000u          /* ... over the first MiB of its RAM */
#define CARD_RAM   0x01000000u          /* memory cards of 16 MiB (D50, D52) */
#define CAUSE_IRQ  0x80000000u
#define USER_TOP   0x80000000u
#define ST_S 1u
#define ST_IE 2u
#define ST_PS 4u
#define ST_PIE 8u
#define TLB_N 64
#define PTE_V 1u
#define PTE_W 2u
#define PTE_X 4u
#define TLBHI_I 0x100u

static uint64_t *ucode;
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

static uint8_t *rom, *ram;

/* the register file and PC */
static uint32_t rf[32], pc;
/* the sequencer */
static uint32_t ir, status = ST_S, imask;
static unsigned useq, uop, ustep, ucond;    /* microaddress */
static uint32_t cause_latch;
static int ffault;                          /* the last fault was in a fetch */
static int mmufault;                        /* the trap being entered came from the MMU / data card */
/* the ALU: operands and function latched by ALUGO; the result is on R from the next cycle */
static uint32_t alua, alub;
static unsigned aluop;
/* MMU / data card */
static uint32_t mar, mdr, asid, tlbhi, tlblo;   /* (MDR, the store data, is the CPU card's) */
static uint32_t mmu_badaddr;                /* BADADDR latch: updated by MMU faults only (D17) */
static struct { uint32_t hi, lo; } tlb[2][TLB_N];
/* I/O card: p32io.h (timer, UARTs, ports, CF; idle_ticks) */

static unsigned long long icount, ucycles;
static FILE *itrace;                            /* -I */
static uint32_t it_pc, it_flags;
static unsigned long long it_cycles;
static int cycle_ticks;
static unsigned wait_states, bus_cycle;   /* -W N; did this microcycle use the bus */
static unsigned card_waits;             /* -C: the wait states the addressed card asks for (the disk card: 2, D66) */
static int bus_locked;                      /* -LOCK held: a locked LOADW's tenure, to its STOREW (D51) */
static uint32_t dma_addr;                   /* -D: the DMA master's word, its period, ... */
static unsigned dma_period, dma_wait;
static int dma_nolock;
static unsigned long long dma_count;        /* ... and how many it has added */
static FILE *strace;

static int super(void) { return (status & ST_S) != 0; }

static void dump(void)
{
    fprintf(stderr, "  pc=%08X ir=%08X uaddr=%u:%02X:%u:%u mode=%s status=%X  (%llu instructions, %llu cycles)\n",
            pc, ir, useq, uop, ustep, ucond, super() ? "S" : "U", status, icount, ucycles);
    for (int i = 0; i < 32; i++)
        fprintf(stderr, "  rf%-2d=%08X%s", i, rf[i], (i % 4 == 3) ? "\n" : "");
}

static void stop(const char *msg)
{
    fflush(stdout);
    fprintf(stderr, "%s\n", msg);
    dump();
    exit(255);
}

/* ---------------------------------------------------------------- I/O card: p32io.h */

/* ---------------------------------------------------------------- MMU / data card */
static unsigned fault_cause;
static uint32_t fault_addr;

static int fault(unsigned code, uint32_t addr)
{
    fault_cause = code;
    fault_addr = addr;
    return 1;
}

/* Virtual -> physical for one access; 1 = fault (fault_cause/fault_addr set). */
static int translate(uint32_t va, int fetch, int store, uint32_t *pa)
{
    if (super()) {
        *pa = va;
        return 0;
    }
    if (va >= USER_TOP)
        return fault(EXC_PRIVILEGE, va);
    unsigned idx = (va >> 12) & (TLB_N - 1);
    uint32_t hi = tlb[fetch][idx].hi, lo = tlb[fetch][idx].lo;
    uint32_t fault_hi = (va & ~0xFFFu) | (fetch ? TLBHI_I : 0) | asid;
    if (!(lo & PTE_V) || (hi >> 12) != (va >> 12) || (hi & 0xFF) != asid) {
        tlbhi = fault_hi;
        return fault(fetch ? EXC_TLBMISS_X : store ? EXC_TLBMISS_W : EXC_TLBMISS_R, va);
    }
    if ((store && !(lo & PTE_W)) || (fetch && !(lo & PTE_X))) {
        tlbhi = fault_hi;
        return fault(EXC_PROTECT, va);
    }
    *pa = (lo & ~0xFFFu) | (va & 0xFFFu);
    return 0;
}

/* Size and signedness of a LOAD/STORE come from the opcode in IR: its low
   bits, the same in every load / store group (D59) -- 0, 1, 2 = byte, half,
   word; signed when bits 3:2 are 0 (LB, LH, LBX, LHX, LBP, LHP). */
static int ir_size(void)
{
    unsigned op = ir >> 24;
    if (op == OP_SWAP) return 4;
    return (op & 3) == 0 ? 1 : (op & 3) == 1 ? 2 : 4;
}
static int ir_signed(void) { unsigned op = ir >> 24; return op != OP_SWAP && !(op & 0xC) && (op & 3) < 2; }

/* A memory cycle. Reads return the raw aligned word as the memory card
   drives it on the bus (all four byte lanes). Returns 1 on a fault. */
static int mem_cycle_sized(unsigned kind, int lsize, uint32_t store_data, uint32_t *read_data);
static int mem_cycle(unsigned kind, uint32_t store_data, uint32_t *read_data)
{
    return mem_cycle_sized(kind, ir_size(), store_data, read_data);
}
/* lsize: a LOAD's or STORE's size (the MMU / data card has it from SIZE on
   the local link); the other kinds are words */
static int mem_cycle_sized(unsigned kind, int lsize, uint32_t store_data, uint32_t *read_data)
{
    int fetch = kind == MEM_FETCH;
    int store = kind == MEM_STORE || kind == MEM_STOREW;
    int size = (kind == MEM_LOAD || kind == MEM_STORE) ? lsize : 4;
    uint32_t pa;
    if (mar & (uint32_t)(size - 1))
        return fault(EXC_MISALIGNED, mar);
    if (kind == MEM_CHECKW)
        return 0;
    if (translate(mar, fetch, store, &pa))
        return 1;
    uint32_t lane = pa & 3;
    if (pa >= IO_BASE) {
        uint32_t v;
        if (t_hw && pa >= IO_DISK && pa < IO_DISK + 64)
            card_waits = 2;             /* (the disk card's ports: two wait states; only if it answers) */
        if (t_hw && io_byte_device(pa, size))
            card_waits = 1;             /* (the I/O card's byte devices: one, D72) */
        if (t_hw && vid_chip_access(pa, size))
            card_waits = 3;             /* (the video card's chip: three, D104) */
        if (t_hw && store && vid_cmd_access(pa, size))
            card_waits = 1;             /* (the video card's PS/2 command: one, D106) */
        if (store) {
            uint32_t mask = size == 4 ? 0xFFFFFFFFu : (1u << (8 * size)) - 1;
            if (io_write(pa, size, (store_data >> (8 * lane)) & mask)) {
                card_waits = 0;
                return fault(EXC_BUSERROR, pa);
            }
        } else {
            if (io_read(pa, size, &v)) {
                card_waits = 0;
                return fault(EXC_BUSERROR, pa);
            }
            *read_data = v << (8 * lane);
        }
        return 0;
    }
    uint8_t *p;
    uint32_t base = pa & ~3u;
    if (base < ROM_SIZE) {
        if (store)
            return fault(EXC_BUSERROR, pa);
        p = rom + base;
    } else if (base >= RAM_BASE && ram_fitted(base)) {
        p = ram + (base - RAM_BASE);
    } else {
        return fault(EXC_BUSERROR, pa);
    }
    if (store) {                                /* byte enables: only this access's lanes */
        for (int i = 0; i < size; i++)
            p[lane + i] = (uint8_t)(store_data >> (8 * (lane + i)));
    } else {
        *read_data = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
    return 0;
}

/* Load steering (the MMU / data card): the raw word's addressed lane(s) to
   bit 0, then extended, onto R. */
static uint32_t load_extract(uint32_t raw, unsigned lane, int size, int sgn)
{
    uint32_t v = raw >> (8 * lane);
    if (size == 1) return sgn ? (uint32_t)(int32_t)(int8_t)v : (v & 0xFF);
    if (size == 2) return sgn ? (uint32_t)(int32_t)(int16_t)v : (v & 0xFFFF);
    return v;
}

/* MDR store steering: replicate the byte / halfword onto every lane. */
static uint32_t mdr_replicate(int size)
{
    if (size == 1) return (mdr & 0xFF) * 0x01010101u;
    if (size == 2) return (mdr & 0xFFFF) * 0x00010001u;
    return mdr;
}

/* ---------------------------------------------------------------- the ALU */
static uint32_t alu(unsigned op, uint32_t a, uint32_t b)
{
    switch (op) {
    case ALU_ADD:  return a + b;
    case ALU_SUB:  return a - b;
    case ALU_AND:  return a & b;
    case ALU_OR:   return a | b;
    case ALU_XOR:  return a ^ b;
    case ALU_NOR:  return ~(a | b);
    case ALU_SLL:  return a << (b & 31);
    case ALU_SRL:  return a >> (b & 31);
    case ALU_SRA:  return (uint32_t)((int32_t)a >> (b & 31));
    case ALU_SLT:  return (int32_t)a < (int32_t)b;
    case ALU_SLTU: return a < b;
    case ALU_MUL:  return (uint32_t)((uint64_t)a * b);
    case ALU_MULH: return (uint32_t)((uint64_t)((int64_t)(int32_t)a * (int32_t)b) >> 32);
    case ALU_MULHU: return (uint32_t)(((uint64_t)a * b) >> 32);
    case ALU_DIV:
        if (b == 0) return 0xFFFFFFFFu;
        if (a == 0x80000000u && b == 0xFFFFFFFFu) return 0x80000000u;
        return (uint32_t)((int32_t)a / (int32_t)b);
    case ALU_DIVU: return b ? a / b : 0xFFFFFFFFu;
    case ALU_REM:
        if (b == 0) return a;
        if (a == 0x80000000u && b == 0xFFFFFFFFu) return 0;
        return (uint32_t)((int32_t)a % (int32_t)b);
    case ALU_REMU: return b ? a % b : a;
    case ALU_PASSA: return a;
    }
    stop("microcode: undefined ALU function");
    return 0;
}

/* ---------------------------------------------------------------- the sequencer */
static uint32_t imm_unit(unsigned mode)
{
    uint32_t i16 = ir & 0xFFFF;
    switch (mode) {
    case IMM_SEXT16: return (uint32_t)(int32_t)(int16_t)i16;
    case IMM_ZEXT16: return i16;
    case IMM_LUI:    return i16 << 16;
    case IMM_BR:     return (uint32_t)(int32_t)(int16_t)i16 << 2;
    case IMM_J:      return (uint32_t)((int32_t)(ir << 8) >> 8) << 2;
    case IMM_FOUR:   return 4;
    case IMM_ZERO:   return 0;
    case IMM_INC8:   return (uint32_t)(int32_t)(int8_t)ir;        /* D59 */
    }
    stop("microcode: undefined immediate mode");
    return 0;
}

static unsigned field_val(unsigned rsel)       /* IR field 0..15 */
{
    switch (rsel) {
    case RSEL_A: return (ir >> 20) & 15;
    case RSEL_B: return (ir >> 16) & 15;
    case RSEL_C: return (ir >> 12) & 15;
    }
    return 15;                                  /* RSEL_SP */
}

/* Register-file address (5 bits): r15 bank rule for IR fields and SP. */
static unsigned reg_addr(unsigned rsel, unsigned rconst)
{
    if (rsel == RSEL_CONST)
        return rconst;
    if (rsel == RSEL_CRMAP)
        return uc_craddr[(ir >> 16) & 15];
    unsigned n = field_val(rsel);
    return (n == 15 && super()) ? 31 : n;
}

static void set_mode(int s)
{
    status = s ? (status | ST_S) : (status & ~ST_S);
}

static uint32_t crhw_read(void)
{
    switch (uc_craddr[(ir >> 16) & 15]) {
    case HWCR_STATUS: return status;
    case HWCR_IPEND:  return io_ipend();
    case HWCR_IMASK:  return imask;
    case HWCR_ASID:   return asid;
    case HWCR_TLBHI:  return tlbhi;
    case HWCR_TLBLO:  return tlblo;
    }
    return 0;
}

static void crhw_write(uint32_t v)
{
    switch (uc_craddr[(ir >> 16) & 15]) {
    case HWCR_STATUS: status = v & 15u; break;
    case HWCR_IPEND:  break;                    /* read-only */
    case HWCR_IMASK:  imask = v & 0xFF; break;
    case HWCR_ASID:   asid = v & 0xFF; break;
    case HWCR_TLBHI:  tlbhi = v & 0xFFFFF1FFu; break;
    case HWCR_TLBLO:  tlblo = v & 0xFFFFF007u; break;
    }
}

static unsigned condition(unsigned c)
{
    switch (c) {
    case COND_ZERO:   return 0;
    case COND_EQ:     return alua == alub;
    case COND_LT:     return (int32_t)alua < (int32_t)alub;
    case COND_LTU:    return alua < alub;
    case COND_CRILL:  return uc_crkind[(ir >> 16) & 15] == 0;
    case COND_CRHW:   return uc_crkind[(ir >> 16) & 15] == 2;
    case COND_FFAULT: return ffault;
    case COND_MMUF:   return mmufault;
    }
    stop("microcode: undefined condition");
    return 0;
}

static void goto_internal(unsigned seq_id)
{
    useq = 1;
    uop = seq_id;
    ustep = 0;
    ucond = 0;
    if ((seq_id == ISEQ_TRAP || seq_id == ISEQ_INTR) && rf[PR_VBR] == 0) {
        char msg[96];
        snprintf(msg, sizeof msg, "EXCEPTION: cause %08X at address %08X (no handler, VBR = 0)",
                 cause_latch, mmufault ? mmu_badaddr : 0u);
        stop(msg);
    }
}

static void state_trace(void)
{
    if (!strace)
        return;
    unsigned cur = super() ? 31 : 15, other = super() ? 15 : 31;
    fprintf(strace, "%08X %c", pc, super() ? 'S' : 'U');
    for (int i = 1; i <= 14; i++)
        fprintf(strace, " %08X", rf[i]);
    fprintf(strace, " %08X o=%08X st=%X epc=%08X cause=%08X bad=%08X\n",
            rf[cur], rf[other], status, rf[PR_EPC], rf[PR_CAUSE], rf[PR_BADADDR]);
}

/* -D: the DMA master's turn: its word + 1, in RAM (a physical address) */
static void dma_cycle(void)
{
    if (dma_addr < RAM_BASE || !ram_fitted(dma_addr))
        stop("-D: the DMA address is not in RAM");
    uint8_t *p = ram + (dma_addr - RAM_BASE);
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    v++;
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    dma_count++;
}

/* One microcycle. */
static int hw_hold;                             /* -C: WAIT holds its microcycle */
static unsigned long long idle_cycles;          /* ... how many it has held (-G) */

/* -G, the measuring gate (D85): bit 7 of the output port. Devices take no
   time here (no drive's access time, no baud rate), so the report gives the
   sectors, commands and bytes for adding those on. */
#define GATE_BIT 0x80
struct gate_count { unsigned long long cycles, instr, idle, rd, wr, cmds, tx; };
static struct gate_count gate_from;
static int gate_on, gate_up;
static unsigned gate_runs;

static struct gate_count gate_now(void)
{
    struct gate_count g = { ucycles, icount, idle_cycles, disk_rd, disk_wr, disk_cmds, con_bytes };
    return g;
}

static const char *commas(unsigned long long n)
{
    static char buf[4][32];
    static int which;
    char *e = buf[which = (which + 1) & 3] + 31;
    int k = 0;
    *e = 0;
    do {
        if (k && k % 3 == 0)
            *--e = ',';
        *--e = (char)('0' + n % 10);
        k++;
    } while (n /= 10);
    return e;
}

static void gate_report(const char *note)
{
    struct gate_count g = gate_now();
    const char *eol = tty_raw ? "\r\n" : "\n";
    unsigned long long cycles = g.cycles - gate_from.cycles, idle = g.idle - gate_from.idle;
    unsigned long long instr = g.instr - gate_from.instr, busy = cycles - idle;
    unsigned mhz = t_hw_div;
    double secs = cycles / (mhz * 1e6);
    unsigned hours = (unsigned)(secs / 3600);
    unsigned mins = (unsigned)(secs / 60) % 60;
    fprintf(stderr, "[cycles #%u%s: %s cycles, %u:%02u:%04.1f at %u MHz (%.3f s)%s", ++gate_runs, note,
            commas(cycles), hours, mins, secs - hours * 3600.0 - mins * 60.0, mhz, secs, eol);
    fprintf(stderr, " %s instructions, %.2f MIPS busy; idle %s cycles (%.1f%%)%s", commas(instr),
            busy ? instr * (double)mhz / busy : 0.0, commas(idle), cycles ? idle * 100.0 / cycles : 0.0, eol);
    fprintf(stderr, " disk %s sectors read, %s written, %s commands; console %s bytes]%s",
            commas(g.rd - gate_from.rd), commas(g.wr - gate_from.wr), commas(g.cmds - gate_from.cmds),
            commas(g.tx - gate_from.tx), eol);
}

static void gate_port(uint8_t v)
{
    int up = (v & GATE_BIT) != 0;
    if (up && !gate_up)
        gate_from = gate_now();
    else if (!up && gate_up)
        gate_report("");
    gate_up = up;
}

static void gate_exit(void)                     /* the machine stopped with the gate up */
{
    if (gate_up)
        gate_report(", the gate still up at the end");
}

/* -q, the profile (D88): at each instruction's start the cycles since the last
   go to the last one's address. Addresses are a process's, so one program at a
   time: cycles cc -S x.c profiles cc. */
#define UPROF_BYTES 0x00800000u                 /* the first 8 MiB of a process */
static FILE *uprof;
static unsigned long long *ucount, *kcount, uprof_kernel, uprof_other, uprof_mark;
static uint32_t uprof_pc;
static int uprof_user;

static void uprof_step(void)
{
    unsigned long long d = ucycles - uprof_mark;
    uprof_mark = ucycles;
    if (gate_up) {
        if (!uprof_user) {
            uprof_kernel += d;
            if (uprof_pc < UPROF_BYTES)
                kcount[uprof_pc >> 2] += d;
        } else if (uprof_pc < UPROF_BYTES)
            ucount[uprof_pc >> 2] += d;
        else
            uprof_other += d;
    }
    uprof_pc = pc, uprof_user = !super();
}

static void uprof_write(void)
{
    fprintf(uprof, "kernel %llu\nother %llu\n", uprof_kernel, uprof_other);
    for (uint32_t k = 0; k < UPROF_BYTES >> 2; k++)
    {
        if (ucount[k])
            fprintf(uprof, "%08X %llu\n", k << 2, ucount[k]);
        if (kcount[k])
            fprintf(uprof, "K%08X %llu\n", k << 2, kcount[k]);
    }
    fclose(uprof);
}
/* -dbg (p32dbg.h): the machine as the debug server sees it. Architectural
   r15 is rf[31] in supervisor mode, rf[15] (USP) in user mode; EPC, CAUSE,
   BADADDR and VBR are rf[16..19] (uc_craddr) */
static uint32_t dbg_ipc;                        /* the instruction under way (its address) */
static unsigned dbg_uidx;                       /* the last microcycle: its microaddress, its buses */
static uint32_t dbg_busa, dbg_busb, dbg_busr;
static int cycle_hooks;                         /* -D or -dbg: cycle() looks at them (set once) */
static int dbg_boundary_now(void) { return useq == 0 && ustep == 0; }
static uint32_t dbg_status_pc(void) { return dbg_boundary_now() ? pc : dbg_ipc; }
static int dbg_supervisor(void) { return super(); }
static unsigned long long dbg_instrs(void) { return icount; }
static unsigned long long dbg_cycles(void) { return ucycles; }

static void dbg_regs(struct sbuf *s)
{
    sb_add(s, "\"r\":[");
    for (int i = 0; i < 16; i++) sb_add(s, "%s%u", i ? "," : "", i == 15 ? rf[super() ? 31 : 15] : rf[i]);
    sb_add(s, "],\"sp_other\":%u,\"status\":%u,\"imask\":%u,\"epc\":%u,\"cause\":%u,\"badaddr\":%u,\"vbr\":%u,"
           "\"asid\":%u,\"tlbhi\":%u,\"tlblo\":%u,\"timer\":[%u,%u,%d,%d],\"ir\":%u,",
           rf[super() ? 15 : 31], status, imask, rf[16], rf[17], rf[18], rf[19], asid, tlbhi, tlblo,
           t_count, t_reload, t_enable, t_pending, ir);
}

static int dbg_set(const char *name, uint32_t v)
{
    static const char *const nm[] = { "epc", "cause", "badaddr", "vbr" };
    char *e;
    if (name[0] == 'r' && name[1]) {
        long n = strtol(name + 1, &e, 10);
        if (*e || n < 1 || n > 15) return 0;    /* (r0 is zero) */
        rf[n == 15 && super() ? 31 : n] = v;
        return 1;
    }
    if (!strcmp(name, "sp")) { rf[super() ? 31 : 15] = v; return 1; }
    if (!strcmp(name, "sp_other")) { rf[super() ? 15 : 31] = v; return 1; }
    if (!strcmp(name, "pc")) { pc = v & ~3u; if (dbg_boundary_now()) dbg_ipc = pc; return 1; }
    if (!strcmp(name, "status")) { status = v; return 1; }
    if (!strcmp(name, "imask")) { imask = v; return 1; }
    if (!strcmp(name, "asid")) { asid = v; return 1; }
    for (int k = 0; k < 4; k++)
        if (!strcmp(name, nm[k])) { rf[16 + k] = v; return 1; }
    return 0;
}

static int dbg_phys(uint32_t a, int phys, uint32_t *pa)  /* as a load would see a (no fault) */
{
    if (phys || super()) { *pa = a; return 1; }
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
    return NULL;
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

static void dbg_cpu_reset(void)                 /* as -RES: the RESET sequence from the next cycle */
{
    memset(tlb, 0, sizeof tlb);
    status = ST_S;
    imask = asid = tlbhi = tlblo = 0;
    t_enable = t_pending = 0;
    goto_internal(ISEQ_RESET);
    dbg_ipc = 0;
}

static int dbg_ustate(struct sbuf *s)
{
    if (!s) return 1;
    sb_add(s, "\"uaddr\":[%u,%u,%u,%u],\"word\":\"%016llx\",\"a\":%u,\"b\":%u,\"r\":%u,"
           "\"alua\":%u,\"alub\":%u,\"aluop\":%u,\"flags\":[%d,%d,%d],\"mar\":%u,\"mdr\":%u,\"ir\":%u,\"rf\":[",
           dbg_uidx >> 13, (dbg_uidx >> 5) & 0xFF, (dbg_uidx >> 1) & 15, dbg_uidx & 1,
           (unsigned long long)ucode[dbg_uidx], dbg_busa, dbg_busb, dbg_busr,
           alua, alub, aluop, alua == alub, (int32_t)alua < (int32_t)alub, alua < alub, mar, mdr, ir);
    for (int i = 0; i < 32; i++) sb_add(s, "%s%u", i ? "," : "", rf[i]);
    sb_add(s, "],\"next\":[%u,%u,%u,%u],", useq, uop, ustep, ucond);
    return 1;
}

static void cycle_body(unsigned long long limit);
static void dma_cycle(void);
static void cycle(unsigned long long limit)
{
    int dma = 0;
    if (DBG_UNLIKELY(cycle_hooks)) {            /* -D or -dbg */
        if (dbg_on)
            dbg_ucycle();                       /* (-dbg: a ustep's last cycle done, stop before this one) */
        if (dma_period && ++dma_wait >= dma_period && (!bus_locked || dma_nolock)) {
            dma_wait = 0;                       /* granted at this boundary: */
            dma_cycle();                        /* its read and write come first */
            dma = 1;
        }
    }
    bus_cycle = 0;
    card_waits = 0;
    vid_ucycle = ucycles;                       /* (the video card's chip: its time at -C timing, */
    if (t_hw)                                   /* and its IRQ as this microcycle begins) */
        vid_irq_seen = vid_fitted && vid_chip_irq();
    cycle_body(limit);
    if (dma && bus_cycle) {                     /* STALL: the CPU's cycle waited two clocks */
        ucycles += 2;
        if (cycle_ticks && !t_hw) { io_tick(); io_tick(); }
    }
    if (t_hw)
        io_hwedge();                            /* the CLK edge ends the cycle */
    if (bus_cycle)
        for (unsigned k = 0; k < wait_states + card_waits; k++) {    /* -WAIT holds the cycle (D50) */
            ucycles++;
            if (t_hw)
                io_hwedge();
            else if (cycle_ticks)
                io_tick();
        }
}

static void cycle_body(unsigned long long limit)
{
    if (useq == 0 && ustep == 0 && DBG_UNLIKELY(dbg_on)) {    /* -dbg: may stop here (a reset moves on) */
        dbg_ipc = pc;
        dbg_boundary(pc);
        dbg_ipc = pc;
    }
    if (useq == 0 && ustep == 0) {              /* instruction boundary: fetch begins */
        if (limit && icount >= limit)
            stop("STOP: instruction limit reached");
        if (uprof)
            uprof_step();
        state_trace();
        if (itrace) {
            if (icount) {
                uint32_t rec[4] = { it_pc, ir, (uint32_t)(ucycles - it_cycles), it_flags };
                fwrite(rec, sizeof rec, 1, itrace);
            }
            it_pc = pc, it_cycles = ucycles, it_flags = super() ? 1 : 0;
        }
        icount++;
        if (!cycle_ticks && !t_hw)
            io_tick();
    }
    if (cycle_ticks && !t_hw)
        io_tick();
    ucycles++;
    /* The interrupt decision at the end of an instruction sees IE and IMASK
       as the cycle began (the registers change at its closing edge): after
       MTCR STATUS / IMASK or RTE one more instruction runs first (D20). */
    uint32_t ie_at_start = status & ST_IE, imask_at_start = imask;

    unsigned uidx = useq << 13 | uop << 5 | ustep << 1 | ucond;
    uint64_t word = ucode[uidx];
    unsigned asrc = UF_ASRC(word), bsrc = UF_BSRC(word), rsrc = UF_RSRC(word), rdst = UF_RDST(word);
    unsigned rconst = UF_RCONST(word), mem = UF_MEM(word), mode = UF_MODE(word);
    bus_cycle = mem == MEM_FETCH || mem == MEM_LOAD || mem == MEM_STORE
             || mem == MEM_LOADW || mem == MEM_STOREW || mem == MEM_IACK;

    /* Decode trap (D18): the decode table flags an illegal or (in user mode)
       privileged opcode during step 1, the first after the fetch. That step
       runs with its effects suppressed by the abort gating, then trap entry
       follows. */
    if (useq == 0 && ustep == 1) {
        unsigned op = ir >> 24;
        unsigned code = (p32_fmt[op] == F_ILLEGAL || (ir & p32_zmask[op])) ? EXC_ILLEGAL
                      : (p32_priv[op] && !super()) ? EXC_PRIVILEGE : 0;
        if (code) {
            cause_latch = code;
            mmufault = 0;
            ffault = 0;
            goto_internal(ISEQ_TRAP);
            return;
        }
    }

    /* The three buses, from the state as the cycle began. */
    uint32_t a = 0, b = 0, r = 0, raw = 0;
    switch (asrc) {
    case ASRC_NONE: break;
    case ASRC_REG:  a = rf[reg_addr(UF_RSA(word), rconst)]; break;
    case ASRC_PC:   a = pc; break;
    default:        stop("microcode: undefined A source");
    }
    switch (bsrc) {
    case BSRC_NONE: break;
    case BSRC_REG:  b = rf[reg_addr(UF_RSB(word), rconst)]; break;
    case BSRC_IMM:  b = imm_unit(UF_IMM(word)); break;
    default:        stop("microcode: undefined B source");
    }
    int faulted = 0, size = mem == MEM_STORE ? ir_size() : 4;
    switch (rsrc) {
    case RSRC_NONE:    break;
    case RSRC_ALU:     r = alu(aluop, alua, alub); break;
    case RSRC_MEM:
        if (mem == MEM_IACK) {                  /* the acknowledge cycle (D57): MAR = CAUSE */
            r = (mar & ~0xFF00u) | io_iack(mar & 7) << 8;
            break;
        }
        faulted = mem_cycle(mem, 0, &raw);
        r = mem == MEM_LOAD ? load_extract(raw, mar & 3, ir_size(), ir_signed()) : raw;
        break;
    case RSRC_IMM:     r = imm_unit(UF_IMM(word)); break;
    case RSRC_MAR:     r = mar; break;
    case RSRC_MDRS:    r = mdr_replicate(size); break;
    case RSRC_CAUSE:   r = cause_latch; break;
    case RSRC_BADADDR: r = mmu_badaddr; break;    /* the MMU / data card drives it */
    case RSRC_CRHW:    r = crhw_read(); break;
    case RSRC_AGU:                              /* the address adder (D59) */
        switch (UF_AOFF(word)) {
        case AOFF_SEXT16: r = a + (uint32_t)(int32_t)(int16_t)ir; break;
        case AOFF_OFF8:   r = a + (uint32_t)(int32_t)(int8_t)(ir >> 8); break;
        case AOFF_INDEX:  r = a + (b << (ir & 3)); break;
        default:          stop("microcode: undefined address offset");
        }
        break;
    default:           stop("microcode: undefined R source");
    }
    dbg_uidx = uidx, dbg_busa = a, dbg_busb = b, dbg_busr = r;   /* (-dbg's ustate: stores, no test) */
    if (!faulted && rsrc != RSRC_MEM && mem != MEM_NONE)
        faulted = mem_cycle(mem, r, &raw);      /* a store (its data on R) or an alignment check */

    if (faulted) {                              /* abort: no effect, enter the trap sequence */
        bus_locked = 0;                         /* (a fault ends a locked tenure) */
        cause_latch = fault_cause;
        mmu_badaddr = fault_addr;
        mmufault = 1;
        ffault = useq == 0 && ustep == 0;
        goto_internal(ISEQ_TRAP);
        return;
    }
    if (mem == MEM_LOADW && UF_LOCK(word))
        bus_locked = 1;                         /* LOCKREQ: -LOCK from this read ... */
    else if (mem == MEM_STOREW)
        bus_locked = 0;                         /* ... to the end of its store */

    /* The clock edge: R's destination, MDR, MAR from the PC, the ALU's latches. */
    switch (rdst) {
    case RDST_NONE: break;
    case RDST_REG: {
        unsigned rsw = UF_RSW(word), ad = reg_addr(rsw, rconst);
        if (rsw <= RSEL_C && field_val(rsw) == 0)
            ad = PR_SINK;                       /* writes to r0 go to the sink */
        rf[ad] = r;
        break;
    }
    case RDST_PC:    pc = r & ~3u; break;       /* the PC holds bits 31:2 */
    case RDST_MAR:   mar = r; break;
    case RDST_PCMAR: pc = r & ~3u; mar = pc; break;   /* (both bits 31:2: a fetch address, D15) */
    case RDST_IR:    ir = r; break;
    case RDST_MDR:   mdr = r; break;
    case RDST_CRHW:  crhw_write(r); break;
    default:         stop("microcode: undefined R destination");
    }
    if (UF_MDRB(word))
        mdr = b;
    if (UF_MARPC(word))
        mar = pc;
    if (UF_MARINC(word))
        mar += 4;                               /* MAR counts (D59) */
    if (UF_ALUGO(word)) {
        alua = a;
        alub = b;
        aluop = UF_ALU(word);
    }
    if (UF_PCINC(word))
        pc += 4;

    switch (mode) {
    case MODE_NONE: break;
    case MODE_TRAPENTER:
        status = (status & ~(ST_PS | ST_PIE)) | (super() ? ST_PS : 0) | ((status & ST_IE) ? ST_PIE : 0);
        set_mode(1);
        status &= ~ST_IE;
        break;
    case MODE_RTE:
        set_mode((status & ST_PS) != 0);
        status = (status & ~ST_IE) | ((status & ST_PIE) ? ST_IE : 0);
        break;
    case MODE_TLBW: {
        int side = (tlbhi & TLBHI_I) != 0;
        unsigned idx = (tlbhi >> 12) & (TLB_N - 1);
        tlb[side][idx].hi = tlbhi & ~TLBHI_I;
        tlb[side][idx].lo = tlblo;
        break;
    }
    case MODE_HALT:
        if (dbg_on) {                           /* -dbg: halted; this microcycle again until a reset */
            dbg_halt("halt");
            return;
        }
        fflush(stdout);
        fprintf(stderr, "HALT at %08X, r1=%08X (%llu instructions, %llu cycles)\n",
                pc - 4, rf[1], icount, ucycles);
        if (dma_period) {
            uint8_t *p = ram + (dma_addr - RAM_BASE);
            fprintf(stderr, "DMA: %llu added, the word is %u\n", dma_count,
                    (unsigned)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24));
        }
        exit((int)(rf[1] & 0xFF));
    case MODE_WAIT:                             /* idle until an enabled line is asserted */
        if (t_hw) {                             /* the sequencer holds this microcycle (-ANYP) */
            if (!(io_ipend() & imask)) {
                if (!t_enable && !t_hw_en && !(vid_fitted && (vid_irq_in() > 0 || ps2_due_in() > 0)))
                    stop("STOP: WAIT with no possible wake-up");
                hw_hold = 1;
            }
        } else if (io_wait(imask)) {
            if (dbg_on) {
                dbg_halt("wait: nothing can end it");
                return;
            }
            stop("STOP: WAIT with no possible wake-up");
        }
        break;
    default: stop("microcode: undefined mode");
    }

    if (hw_hold) {                              /* WAIT, nothing pending: same microaddress */
        hw_hold = 0;
        idle_cycles++;
        return;
    }
    /* Next microaddress. The condition is taken on the state after the edge. */
    unsigned c = condition(UF_COND(word));
    switch (UF_SEQ(word)) {
    case SEQ_NEXT:
        ustep++;
        ucond = c;
        if (useq == 0 && ustep == 1)            /* IR now holds the instruction */
            uop = ir >> 24;
        break;
    case SEQ_END: {                             /* instruction done: interrupt or fetch */
        uint32_t lines = io_ipend() & imask_at_start;
        if (ie_at_start && lines && mode != MODE_TRAPENTER) {   /* (not at trap entry's end, D20) */
            unsigned n = 0;
            while (!(lines & (1u << n)))
                n++;
            cause_latch = CAUSE_IRQ | n;
            mmufault = 0;
            goto_internal(ISEQ_INTR);
        } else {
            useq = 0;
            ustep = 0;
            ucond = 0;
        }
        break;
    }
    case SEQ_TRAP:                              /* microcode-raised exception */
        cause_latch = UF_RAISE(word);
        mmufault = 0;
        ffault = 0;
        goto_internal(ISEQ_TRAP);
        break;
    default: stop("microcode: undefined sequencer code");
    }
}

static void *load_file(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(255);
    }
    uint8_t *buf = calloc(max, 1);
    *len = fread(buf, 1, max, f);
    if (fgetc(f) != EOF) {
        fprintf(stderr, "p32uemu: %s is too large\n", path);
        exit(255);
    }
    fclose(f);
    return buf;
}

/* the disk card's DMA engine (D75): physical memory as the bus answers it
   (a write to the ROM, or nothing there: no -ACK) */
static uint8_t *dma_phys(uint32_t a, int write)
{
    if (a < ROM_SIZE)
        return write ? NULL : rom + a;
    if (a >= RAM_BASE && ram_fitted(a))
        return ram + (a - RAM_BASE);
    return NULL;
}

int main(int argc, char **argv)
{
    unsigned long long limit = 100000000ULL;
    int limit_set = 0;
    const char *image = NULL, *ufile = "../microcode/p32ucode.bin";
    int used;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u") && i + 1 < argc)
            ufile = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            if (!(strace = fopen(argv[++i], "w"))) {
                perror(argv[i]);
                return 255;
            }
        } else if (!strcmp(argv[i], "-I") && i + 1 < argc) {
            if (!(itrace = fopen(argv[++i], "wb"))) {
                perror(argv[i]);
                return 255;
            }
        } else if (!strcmp(argv[i], "-l") && i + 1 < argc)
            limit = strtoull(argv[++i], NULL, 0), limit_set = 1;
        else if (!strcmp(argv[i], "-c"))
            cycle_ticks = 1;
        else if (!strcmp(argv[i], "-W") && i + 1 < argc)
            wait_states = (unsigned)strtoul(argv[++i], NULL, 0);
        else if ((!strcmp(argv[i], "-D") || !strcmp(argv[i], "-Dnolock")) && i + 1 < argc) {
            char *e;
            dma_nolock = argv[i][2] != 0;
            dma_addr = (uint32_t)strtoul(argv[++i], &e, 0);
            dma_period = *e == ':' ? (unsigned)strtoul(e + 1, NULL, 0) : 0;
            if (!dma_period || (dma_addr & 3)) { fprintf(stderr, "-D ADDR:PERIOD (an aligned word)\n"); return 255; }
        } else if (!strcmp(argv[i], "-C"))
            t_hw = 1;                           /* the I/O card's timer exactly */
        else if (!strcmp(argv[i], "-F"))
            t_hw_div = 4;                       /* the 4 MHz clock jumper (D74) */
        else if (!strcmp(argv[i], "-G"))
            gate_on = t_hw = 1;                 /* the measuring gate (D85) */
        else if (!strcmp(argv[i], "-q") && i + 1 < argc) {      /* its profile (D88) */
            if (!(uprof = fopen(argv[++i], "w")) || !(ucount = calloc(UPROF_BYTES >> 2, sizeof *ucount)) || !(kcount = calloc(UPROF_BYTES >> 2, sizeof *kcount))) {
                perror(argv[i]);
                return 255;
            }
        }
        else if ((used = ram_option(argc, argv, i)) != 0) {
            if (used < 0)
                return 255;
            i++;
        } else if ((used = io_option(argc, argv, i)) != 0) {
            if (used < 0)
                return 255;
            i += used - 1;
        } else if (argv[i][0] != '-' && !image)
            image = argv[i];
        else
            image = NULL, i = argc;
    }
    if (!image) {
        fprintf(stderr, "usage: p32uemu [-u ucode.bin] [-s FILE] [-l N] [-c] [-C [-F]] [-G [-q FILE]] [-W N] [-D ADDR:PERIOD] [-M CARDS] [-d IMG] [-a OUT] [-A IN] [-p N] image.bin\n" IO_USAGE);
        return 255;
    }
    io_init(0);
    dma_map = dma_phys;
    if (uprof && !gate_on) {
        fprintf(stderr, "-q needs -G\n");
        return 255;
    }
    if (gate_on) {
        port_hook = gate_port;
        atexit(gate_exit);
    }
    if (uprof)
        atexit(uprof_write);
    if (cycle_ticks || t_hw)
        dma_ticks = 11;                         /* (a tick a microcycle: the card's clocks a word) */
    if (tty_raw && !limit_set)
        limit = 0;                      /* interactive: run until stopped (^], halt) */
    size_t n;
    ucode = load_file(ufile, UC_WORDS * 8, &n);
    if (n != UC_WORDS * 8) {
        fprintf(stderr, "p32uemu: %s is not a %d-word control store\n", ufile, UC_WORDS);
        return 255;
    }
    rom = load_file(image, ROM_SIZE, &n);
    ram = calloc(ram_end() - RAM_BASE, 1);
    if (!ram)
        return 255;
    goto_internal(ISEQ_RESET);                  /* reset: S = 1, IE = 0, sequence RESET */
    if (dbg_port >= 0) {                        /* -dbg: the front end drives it, without a limit unless -l */
        dbg_name = "p32uemu";
        dbg_start();
        if (!limit_set) limit = 0;
    }
    cycle_hooks = dma_period || dbg_on;
    for (;;)
        cycle(limit);
}
