/*
 * p32busmodel.c -- the MMU / data card, memory and the devices, as the CPU
 * card's gate-level simulation sees them over J3 (generators/sim_cpu.py,
 * docs/cpu-card.md). It is p32uemu's own model of those cards -- this file
 * includes p32uemu.c -- behind a small interface, so the simulation checks
 * the CPU card against exactly the semantics the microcode was verified on.
 *
 *     cc -O2 -shared -fPIC -o libp32bus.so p32busmodel.c     (sim_cpu.py does this)
 */
#define main p32uemu_main
#include "p32uemu.c"
#undef main

static uint32_t spc;                    /* the MMU / data card's shadow PC (D56) */

int bm_init(const char *image)
{
    size_t n;
    io_init(0);
    rom = load_file(image, ROM_SIZE, &n);
    ram = calloc(ram_end() - RAM_BASE, 1);
    return ram ? 0 : -1;
}

void bm_super(int s) { status = s ? ST_S : 0; }        /* (translate() reads STATUS.S) */
uint32_t bm_mar(void) { return mar; }
uint32_t bm_alu(unsigned op, uint32_t a, uint32_t b) { return alu(op, a, b); }
void bm_tick(void) { io_tick(); }
uint32_t bm_ipend(void) { return io_ipend(); }
int bm_wait(uint32_t im) { return io_wait(im); }

/* This microcycle's memory cycle and R source on the MMU / data card, as
   p32uemu's cycle_body does them (once a committed cycle: reads of device
   registers have effects). Returns 1 on a fault (*fc = its cause); *drive:
   the card drives R with *out. size: 1, 2, 4 (from SIZE). */
int bm_cycle(unsigned mem, unsigned rsrc, unsigned size, unsigned sgn, unsigned crsel,
             uint32_t lr_in, uint32_t *out, int *drive, unsigned *fc)
{
    int faulted = 0;
    uint32_t raw = 0;
    *drive = 0;
    *out = 0;
    switch (rsrc) {
    case RSRC_MEM:
        if (mem == MEM_IACK)                    /* the acknowledge cycle (D57): MAR = CAUSE */
            *out = (mar & ~0xFF00u) | io_iack(mar & 7) << 8;
        else {
            faulted = mem_cycle_sized(mem, (int)size, 0, &raw);
            *out = mem == MEM_LOAD ? load_extract(raw, mar & 3, (int)size, (int)sgn) : raw;
        }
        *drive = 1;
        break;
    case RSRC_MAR:     *out = mar; *drive = 1; break;
    case RSRC_BADADDR: *out = mmu_badaddr; *drive = 1; break;
    case RSRC_CRHW:
        if (crsel == HWCR_ASID)  { *out = asid; *drive = 1; }
        if (crsel == HWCR_TLBHI) { *out = tlbhi; *drive = 1; }
        if (crsel == HWCR_TLBLO) { *out = tlblo; *drive = 1; }
        break;
    }
    if (!faulted && rsrc != RSRC_MEM && mem != MEM_NONE)
        faulted = mem_cycle_sized(mem, (int)size, lr_in, &raw);   /* a store (its data on R), or CHECKW */
    *fc = fault_cause;
    return faulted;
}

/* The commit edge on the MMU / data card: the J3 lines as they were before
   it (RDST and the PC / MAR lines already gated by the CPU card's abort). */
void bm_edge(unsigned rdst, uint32_t lr, int pcinc, int marpc, int marinc, int tlbw, unsigned crsel, int faulted)
{
    if (faulted) {
        mmu_badaddr = fault_addr;               /* (the fault's only effect here) */
        return;
    }
    switch (rdst) {
    case RDST_PC:    spc = lr & ~3u; break;
    case RDST_MAR:   mar = lr; break;
    case RDST_PCMAR: spc = lr & ~3u; mar = spc; break;
    case RDST_CRHW:
        if (crsel == HWCR_ASID)  asid = lr & 0xFF;
        if (crsel == HWCR_TLBHI) tlbhi = lr & 0xFFFFF1FFu;
        if (crsel == HWCR_TLBLO) tlblo = lr & 0xFFFFF007u;
        break;
    }
    if (marpc)
        mar = spc;
    if (marinc)
        mar += 4;
    if (pcinc)
        spc += 4;
    if (tlbw) {
        int side = (tlbhi & TLBHI_I) != 0;
        unsigned idx = (tlbhi >> 12) & (TLB_N - 1);
        tlb[side][idx].hi = tlbhi & ~TLBHI_I;
        tlb[side][idx].lo = tlblo;
    }
}

/* ---- the system bus, for the two-card simulation (generators/sim_sys.py):
   the memory cards and the devices as bus slaves */

/* A physical bus cycle: word address a (A31:A2 << 2), byte enables be.
   Returns 1 if a card answered (-ACK); a read's word in *out. */
int bm_phys(int write, uint32_t a, unsigned be, uint32_t data, uint32_t *out)
{
    unsigned lane = 0, size = 0;
    while (lane < 4 && !(be >> lane & 1))
        lane++;
    for (unsigned i = 0; i < 4; i++)
        size += be >> i & 1;
    if (!size)
        return 0;
    uint32_t pa = a + lane;
    *out = 0;
    if (pa >= IO_BASE) {
        uint32_t v, mask = size == 4 ? 0xFFFFFFFFu : (1u << (8 * size)) - 1;
        if (write)
            return !io_write(pa, (int)size, (data >> (8 * lane)) & mask);
        if (io_read(pa, (int)size, &v))
            return 0;
        *out = v << (8 * lane);
        return 1;
    }
    uint8_t *p;
    if (a < ROM_SIZE) {
        if (write)
            return 0;                   /* the ROM does not answer a write */
        p = rom + a;
    } else if (a >= RAM_BASE && ram_fitted(a)) {
        p = ram + (a - RAM_BASE);
    } else if (vid_ram(a)) {
        p = vid_ram(a);
    } else
        return 0;
    if (write) {
        for (unsigned i = 0; i < 4; i++)
            if (be >> i & 1)
                p[i] = (uint8_t)(data >> (8 * i));
    } else
        *out = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return 1;
}

/* The interrupt acknowledge cycle on the bus: the card on level n answers
   with its vector (1), or none does (0: the data lines float high, FF). */
int bm_iack(unsigned n, uint32_t *vec)
{
    unsigned v = io_iack(n);
    *vec = v;
    return v != 0xFF;
}
