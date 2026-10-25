/*
 * p32uemu.c -- P32W microcode-level emulator (docs/microarch.md, D14).
 *
 * Runs the real control-store image built by microcode/p32ucode.py, one
 * microcycle at a time, over a model of the CPU card and the MMU / data card:
 *   sequencer: microaddress {SEQ, OPCODE, STEP, COND}, IR, immediate unit,
 *             STATUS, IMASK, interrupt priority, cause/fault latches,
 *             CR decode table
 *   registers: 32 x 32 SRAM register file (r15 bank rule; writes to r0 go
 *             to the write sink, physical 28), PC counter (bits 31:2)
 *   ALU:      operand latches A, B; function; compare outputs EQ/LT/LTU
 *   MMU / data: MAR, split I/D TLB, ASID/TLBHI/TLBLO, alignment and upper-half
 *             checks, MDR with byte-lane steering
 *   memory, devices: ROM, RAM, the devices of p32io.h (as in p32emu)
 * One shared data bus: each cycle exactly one source (DOE) drives it and one
 * destination (DLD) latches it at the clock edge. A fault in a memory cycle
 * suppresses that cycle's effects and enters the trap sequence.
 *
 * It knows no instruction semantics: those live only in the microcode. It is
 * checked against the instruction-level reference emulator p32emu (D7) by
 * comparing their -s state traces instruction by instruction.
 *
 * Usage: p32uemu [-u ucode.bin] [-s FILE] [-l N] [-c] [-W N] [-D ADDR:PERIOD] [-M CARDS] image.bin
 *   -u F  control-store image (default ../microcode/p32ucode.bin)
 *   -s F  architectural state at every instruction start (as p32emu -s)
 *   -l N  stop after N instructions (default 100000000, or no limit when
 *         the console is a terminal; 0 = no limit)
 *   -M N  memory cards fitted, 1 (default) to 4, 16 MiB each, at the windows
 *         0 .. N-1 (D52): RAM is 1 MiB up to N x 16 MiB; above it a BUSERROR
 *   -c    timer ticks once per microcycle (hardware timing) instead of once
 *         per instruction (the reference emulator's timing, D11)
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "p32opc.h"
#include "p32uc.h"
#include "p32io.h"                     /* the I/O card, shared with p32emu (D50) */

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
static unsigned mem_cards = 1;
static int ram_fitted(uint32_t a)
{
    return a < mem_cards * CARD_RAM;
}
static int ram_option(int argc, char **argv, int i)    /* -M N: 1 (used one argument), -1 bad, 0 not -M */
{
    if (strcmp(argv[i], "-M") || i + 1 >= argc) return 0;
    int n = atoi(argv[i + 1]);
    if (n < 1 || n > 4) { fprintf(stderr, "-M: 1 to 4 (memory cards of 16 MiB)\n"); return -1; }
    mem_cards = (unsigned)n;
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
/* the ALU */
static uint32_t alua, alub;
/* MMU / data card */
static uint32_t mar, mdr, asid, tlbhi, tlblo;
static uint32_t mmu_badaddr;                /* BADADDR latch: updated by MMU faults only (D17) */
static struct { uint32_t hi, lo; } tlb[2][TLB_N];
static int mdr_lane, mdr_size, mdr_signed;  /* how MDRX steers the last load */
/* I/O card: p32io.h (timer, UARTs, ports, CF; idle_ticks) */

static unsigned long long icount, ucycles;
static int cycle_ticks;
static unsigned wait_states, bus_cycle;   /* -W N; did this microcycle use the bus */
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

/* Size and signedness of a LOAD/STORE come from the opcode in IR. */
static int ir_size(void)
{
    switch (ir >> 24) {
    case OP_LB: case OP_LBU: case OP_SB: return 1;
    case OP_LH: case OP_LHU: case OP_SH: return 2;
    default: return 4;
    }
}
static int ir_signed(void) { return (ir >> 24) == OP_LB || (ir >> 24) == OP_LH; }

/* A memory cycle. Reads return the raw aligned word as the memory card
   drives it on the bus (all four byte lanes). Returns 1 on a fault. */
static int mem_cycle(unsigned kind, uint32_t store_data, uint32_t *read_data)
{
    int fetch = kind == MEM_FETCH;
    int store = kind == MEM_STORE || kind == MEM_STOREW;
    int size = (kind == MEM_LOAD || kind == MEM_STORE) ? ir_size() : 4;
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
        if (store) {
            uint32_t mask = size == 4 ? 0xFFFFFFFFu : (1u << (8 * size)) - 1;
            if (io_write(pa, size, (store_data >> (8 * lane)) & mask))
                return fault(EXC_BUSERROR, pa);
        } else {
            if (io_read(pa, size, &v))
                return fault(EXC_BUSERROR, pa);
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
    } else if (vid_ram(base)) {
        p = vid_ram(base);              /* the video card's RAM (D52) */
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

/* MDR load steering: bring the addressed lane(s) to bit 0, then extend. */
static uint32_t mdr_extract(void)
{
    uint32_t v = mdr >> (8 * mdr_lane);
    if (mdr_size == 1) return mdr_signed ? (uint32_t)(int32_t)(int8_t)v : (v & 0xFF);
    if (mdr_size == 2) return mdr_signed ? (uint32_t)(int32_t)(int16_t)v : (v & 0xFFFF);
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
static void cycle_body(unsigned long long limit);
static void dma_cycle(void);
static void cycle(unsigned long long limit)
{
    int dma = 0;
    if (dma_period && ++dma_wait >= dma_period && (!bus_locked || dma_nolock)) {
        dma_wait = 0;                           /* granted at this boundary: */
        dma_cycle();                            /* its read and write come first */
        dma = 1;
    }
    bus_cycle = 0;
    cycle_body(limit);
    if (dma && bus_cycle) {                     /* STALL: the CPU's cycle waited two clocks */
        ucycles += 2;
        if (cycle_ticks && !t_hw) { io_tick(); io_tick(); }
    }
    if (t_hw)
        io_hwedge();                            /* the CLK edge ends the cycle */
    if (bus_cycle)
        for (unsigned k = 0; k < wait_states; k++) {    /* -WAIT holds the cycle (D50) */
            ucycles++;
            if (t_hw)
                io_hwedge();
            else if (cycle_ticks)
                io_tick();
        }
}

static void cycle_body(unsigned long long limit)
{
    if (useq == 0 && ustep == 0) {              /* instruction boundary: fetch begins */
        if (limit && icount >= limit)
            stop("STOP: instruction limit reached");
        state_trace();
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

    uint64_t word = ucode[useq << 13 | uop << 5 | ustep << 1 | ucond];
    unsigned doe = UF_DOE(word), dld = UF_DLD(word), rsel = UF_RSEL(word);
    unsigned rconst = UF_RCONST(word), mem = UF_MEM(word), mode = UF_MODE(word);
    bus_cycle = mem == MEM_FETCH || mem == MEM_LOAD || mem == MEM_STORE
             || mem == MEM_LOADW || mem == MEM_STOREW;

    /* Decode trap (D18): the decode table flags an illegal
       or (in user mode) privileged opcode during step 2. That step runs with
       its effects suppressed by the abort gating, then trap entry follows. */
    if (useq == 0 && ustep == 2) {
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

    /* The bus: exactly one source. */
    uint32_t bus = 0, raw = 0;
    int faulted = 0;
    switch (doe) {
    case DOE_NONE:    break;
    case DOE_REG:     bus = rf[reg_addr(rsel, rconst)]; break;
    case DOE_ALU:     bus = alu(UF_ALU(word), alua, alub); break;
    case DOE_IMM:     bus = imm_unit(UF_IMM(word)); break;
    case DOE_PC:      bus = pc; break;
    case DOE_MEM:     faulted = mem_cycle(mem, 0, &raw); bus = raw; break;
    case DOE_MDRX:    bus = mdr_extract(); break;
    case DOE_MDRS:    bus = mdr_replicate(mem == MEM_STORE ? ir_size() : 4); break;
    case DOE_MAR:     bus = mar; break;
    case DOE_CAUSE:   bus = cause_latch; break;
    case DOE_BADADDR: bus = mmu_badaddr; break;   /* the MMU / data card drives it */
    case DOE_CRHW:    bus = crhw_read(); break;
    default:          stop("microcode: undefined bus source");
    }
    if (!faulted && doe != DOE_MEM && mem != MEM_NONE)
        faulted = mem_cycle(mem, bus, &raw);    /* store or alignment check */

    if (faulted) {                              /* abort: no effect, enter the trap sequence */
        bus_locked = 0;                         /* (a fault ends a locked tenure) */
        cause_latch = fault_cause;
        mmu_badaddr = fault_addr;
        mmufault = 1;
        ffault = useq == 0 && ustep == 1;
        goto_internal(ISEQ_TRAP);
        return;
    }
    if (mem == MEM_LOADW && UF_LOCK(word))
        bus_locked = 1;                         /* LOCKREQ: -LOCK from this read ... */
    else if (mem == MEM_STOREW)
        bus_locked = 0;                         /* ... to the end of its store */

    /* The clock edge: the destination latches the bus. */
    switch (dld) {
    case DLD_NONE: break;
    case DLD_REG: {
        unsigned a = reg_addr(rsel, rconst);
        if (rsel <= RSEL_C && field_val(rsel) == 0)
            a = PR_SINK;                        /* writes to r0 go to the sink */
        rf[a] = bus;
        break;
    }
    case DLD_ALUA: alua = bus; break;
    case DLD_ALUB: alub = bus; break;
    case DLD_IR:   ir = bus; break;
    case DLD_PC:   pc = bus & ~3u; break;     /* the PC holds bits 31:2 */
    case DLD_MAR:  mar = bus; break;
    case DLD_MDR:
        mdr = bus;
        if (mem == MEM_LOAD || mem == MEM_LOADW) {
            mdr_lane = mar & 3;
            mdr_size = mem == MEM_LOAD ? ir_size() : 4;
            mdr_signed = mem == MEM_LOAD && ir_signed();
        } else {
            mdr_lane = 0;
            mdr_size = 4;
            mdr_signed = 0;
        }
        break;
    case DLD_CRHW: crhw_write(bus); break;
    default:       stop("microcode: undefined bus destination");
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
                if (!t_enable && !t_hw_en)
                    stop("STOP: WAIT with no possible wake-up");
                hw_hold = 1;
            }
        } else if (io_wait(imask))
            stop("STOP: WAIT with no possible wake-up");
        break;
    default: stop("microcode: undefined mode");
    }

    if (hw_hold) {                              /* WAIT, nothing pending: same microaddress */
        hw_hold = 0;
        return;
    }
    /* Next microaddress. The condition is taken on the state after the edge. */
    unsigned c = condition(UF_COND(word));
    switch (UF_SEQ(word)) {
    case SEQ_NEXT:
        ustep++;
        ucond = c;
        if (useq == 0 && ustep == 2)            /* IR now holds the instruction */
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
        fprintf(stderr, "usage: p32uemu [-u ucode.bin] [-s FILE] [-l N] [-c] [-W N] [-D ADDR:PERIOD] [-M CARDS] [-d IMG] [-a OUT] [-A IN] [-p N] image.bin\n" IO_USAGE);
        return 255;
    }
    io_init(0);
    if (tty_raw && !limit_set)
        limit = 0;                      /* interactive: run until stopped (^], halt) */
    size_t n;
    ucode = load_file(ufile, UC_WORDS * 8, &n);
    if (n != UC_WORDS * 8) {
        fprintf(stderr, "p32uemu: %s is not a %d-word control store\n", ufile, UC_WORDS);
        return 255;
    }
    rom = load_file(image, ROM_SIZE, &n);
    ram = calloc(mem_cards * CARD_RAM - RAM_BASE, 1);
    if (!ram)
        return 255;
    goto_internal(ISEQ_RESET);                  /* reset: S = 1, IE = 0, sequence RESET */
    for (;;)
        cycle(limit);
}
