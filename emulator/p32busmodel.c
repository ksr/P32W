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
    dma_map = dma_phys;                 /* (the disk card's DMA engine, D75) */
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

/* ---- the I/O card at gate level (generators/sim_sys.py --io): its chips
   whose insides are p32io.h's models -- the UARTs, the clock, the CH376S
   modules, the network coprocessor's firmware -- as the card's wiring
   reaches them; the timer, the decoding, the interrupts and the mailbox's
   latches are the card's own logic then, so they are not used here. */

int bm_option(int argc, char **argv)            /* the emulators' I/O options (-a2 FILE, -p N, -U IMG, -N ...) */
{
    for (int i = 0; i < argc; ) {
        int n = io_option(argc, argv, i);
        if (n <= 0) return -1;
        i += n;
    }
    return 0;
}

unsigned bm_uart_read(int n, unsigned reg) { return uart_read(&uart[n], reg & 7); }
void bm_uart_write(int n, unsigned reg, unsigned v) { uart_write(&uart[n], reg & 7, (uint8_t)v); }
int bm_uart_irq(int n) { return uart_irq(&uart[n]); }

void bm_rtc_index(unsigned v) { rtc_idx = (uint8_t)(v & 0x7F); }
unsigned bm_rtc_read(void) { return rtc_read(); }
void bm_rtc_write(unsigned v) { rtc_write((uint8_t)v); }

unsigned bm_usb_read(int n, unsigned reg) { usb_on(n); unsigned v = usb_read(reg & 1); usb_on(0); return v; }
void bm_usb_write(int n, unsigned reg, unsigned v) { usb_on(n); usb_write(reg & 1, (uint8_t)v); usb_on(0); }
int bm_usb_int(int n) { return usbs[n].intr; }

/* the coprocessor's side of the mailbox: its firmware running (READY), its
   events (ATTN), a request byte taken from the request latch, a reply byte
   for the reply latch, RESET (its RUN pin) */
int bm_net_ready(void) { return net.on; }
int bm_net_attn(void) { if (net.on && ++net.polls >= 256) { net.polls = 0; net_poll(); } return net.attn; }
void bm_net_put(unsigned v) { net_write(0, (uint8_t)v); }
int bm_net_avail(void) { return net.on && net.prep < net.nrep; }
unsigned bm_net_get(void) { return net.rep[net.prep++]; }
void bm_net_reset(void) { int ien = net.ien; net_write(1, 0x80); net.ien = ien; }

/* the other cards' interrupts (the disk card, the video card's PS/2), and
   their acknowledge cycle: the bus model still plays them */
static int disk_gate;                   /* the disk card is at gate level too: not the bus model's */
static int video_gate;                  /* the video card, likewise */
void bm_disk_gate(int on) { disk_gate = on; }
void bm_video_gate(int on) { video_gate = on; }
uint32_t bm_ipend_other(void)
{
    return io_ipend() & ((disk_gate ? 0 : 1u << IRQ_DISK) | (video_gate ? 0 : 1u << IRQ_PS2));
}
int bm_iack_other(unsigned n, uint32_t *vec)
{
    unsigned v = ((n == IRQ_DISK && !disk_gate) || (n == IRQ_PS2 && !video_gate)) ? io_iack(n) : 0xFF;
    *vec = v;
    return v != 0xFF;
}

/* the disk card at gate level: a drive (or two) on port p's cable, as
   p32io.h's model of them answers its registers. reg: CS1 * 8 + DA; size 2
   for the 16-bit data register. bm_ata_present: anything on the port (an
   empty port: the card's pull-downs) */
int bm_ata_present(unsigned p) { return atas[p][0].img || atas[p][1].img; }
unsigned bm_ata_read(unsigned p, unsigned reg, int size)
{
    uint32_t v = 0;
    io_read(IO_DISK + 16 * p + reg, size, &v);
    return v;
}
void bm_ata_write(unsigned p, unsigned reg, int size, unsigned v)
{
    io_write(IO_DISK + 16 * p + reg, size, v);
}
int bm_ata_intrq(unsigned p) { return ata_intrq(p); }

/* the video card at gate level: the PS/2 devices on its sockets, as p32io.h
   models them -- the keyboard types -K FILE's text, both answer a command
   byte (FA; a reset FF: FA AA, the mouse its ID 00) */
int bm_ps2_avail(int port) { return ps2_full(port ? &mouse : &kbd); }
unsigned bm_ps2_get(int port) { return ps2_get(port ? &mouse : &kbd); }
void bm_ps2_command(int port, unsigned c) { ps2_command(port ? &mouse : &kbd, (uint8_t)c); }
