/* core.c -- the debug card's firmware, apart from its hardware (docs/debug-card-theory.md; D135).
 *
 * The card is a set of registers on an 8-bit local bus (theory section 4). This file turns the host's
 * commands into reads and writes on that bus, keeps copies of the registers the card cannot read back, and
 * waits on the card where its sequences need it. Nothing here knows of a Pico: hal.h is all it uses, given
 * by pico.c on the card and by host.c on the Mac, where generators/sim_dbgfw.py runs this very code against
 * the card's gate-level netlist (make test).
 *
 * Where a line sits on the card (its slice and bit) is slices.h, written from the netlist.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hal.h"
#include "core.h"
#include "slices.h"

/* ---------------------------------------------------------------- the local bus map (theory section 4) */
enum { XA_CTRL0 = 0x40, XA_CTRL1, XA_POSTL, XA_POSTH, XA_GO, XA_ARM, XA_STEP, XA_CSTEP,
       XA_TSL, XA_TSH, XA_TRA, XA_TRB, XA_CAPCLR, XA_DISP, XA_IRQ };
enum { XA_STATUS = 0x40, XA_CAL = 0x41, XA_CAH = 0x42 };       /* (reads) */
/* CTRL0 */
#define GD      0x01                    /* alone: D0-31's drive registers on */
#define GBUS    0x02                    /* alone: the bus cycle's (A, BE, the strobes) */
#define GSYS    0x04                    /* alone: -RES, -BREQ3-10, -FAST; with the MMU / data card out, the grants */
#define GLR     0x08                    /* alone: LR0-31 */
#define GJ3     0x10                    /* alone: the CPU card's J3 control lines */
#define ALONE   0x20                    /* the card makes CLK; the groups may drive; a GO needs no grant */
#define ODACK   0x40                    /* -ACK pulled low (DRIVE only) */
#define ODWAIT  0x80                    /* -WAIT pulled low (DRIVE only) */
#define GROUPS  (GD | GBUS | GSYS | GLR | GJ3)
/* CTRL1 */
#define ODLOCK  0x01                    /* -LOCK pulled low (DRIVE only) */
#define CLKRUN  0x02                    /* DBCLK runs: CLK alone, XCLK with XCLKEN */
#define SCLKSEL 0x04                    /* the sample clock is 12 MHz, not CLK */
#define SETUP   0x08                    /* the trigger RAMs on the local bus; no trigger */
#define TNOW    0x10                    /* trigger now */
#define LIVE    0x20                    /* the slices read the latches, not the SRAMs */
#define DMAWR   0x40                    /* a DMA cycle drives D: a write */
#define XCLKEN  0x80                    /* -XCLKEN low: the CPU card on XCLK (D76) */
/* the status register */
#define S_DRIVE 0x01                    /* the mode switch at DRIVE */
#define S_BGNT0N 0x02                   /* -BGNT0: 0 while slot 0 is granted */
#define S_GOQ   0x04                    /* a GO not yet done */
#define S_ACT   0x08                    /* a capture running (RUN or POST) */
#define S_DONE  0x10                    /* the capture has finished */
#define S_ARMQ  0x20                    /* armed, not yet running */
#define S_TRIG  0x40                    /* the trigger condition, now */
#define S_SNAP  0x80                    /* the clock after a DMA cycle */

#define SL(p) ((p) >> 3)
#define BIT(p) ((p) & 7)
#define NSAMPLE 32768                   /* the capture SRAMs (CY7C199) and the trigger RAMs: 32 K each */

/* the firmware's own figures, for its waits (the card's oscillator: 24 MHz, 41.7 ns) */
#define STEP_NS 500                     /* after a STEP: a CLK period alone is eight oscillator periods, 333 ns */
#define LIVE_NS 500                     /* the latches at 12 MHz: a fresh sample or more */
#define GO_US 20000                     /* a DMA cycle's grant, beside a running CPU (another master, -LOCK) */
#define DONE_US 1000000                 /* the post-trigger samples after a STOP, at the slowest sample clock */
#define VCC_MIN_MV 4000                 /* the card's 5 V, through its divider and the Pico's ADC: on above this (D136) */
#define MRES    0x80                    /* TSAH's bit 7: PWROK pulled low, the machine held in reset (DRIVE only, D136) */

/* the display's codes (D100: the meaning is the firmware's): the high digit what the card is doing, the low
 * digit what the last command ran into */
enum { E_OK, E_POWER, E_TIME, E_NOACK, E_REFUSED, E_SYNTAX };
enum { D_IDLE, D_ARMED, D_CAPTURING, D_DONE, D_ALONE, D_XCLK };

static struct {
    int alive;                          /* the card answered the probe */
    uint8_t ctrl0, ctrl1, irq, disp;    /* what the card's write-only registers hold */
    uint16_t post, ts;
    uint8_t drv[NDRIVE];
    uint32_t drv_known;                 /* bit k: drv[k] is what drive register k holds (they are never cleared) */
    int clk_at3;                        /* DBCLK's phase counter at phase 3: a STEP is then a whole CLK period */
    int disp_known;                     /* the display's latches hold disp (they are never cleared) */
    int disp_host;                      /* the host wrote the display: the firmware leaves it */
    int err;                            /* the last command's E_ code */
    int armed;                          /* ARM given; the capture not yet read back */
    int have_end;                       /* end: the capture address when the capture was seen done */
    uint16_t end;                       /*  (bit 15: the buffer went round) */
    int trig_valid;                     /* the trigger RAMs hold trig_a and trig_b */
    int alone_mmu;                      /* ALONE ON found the MMU / data card in */
    int mres;                           /* TSAH's bit 7 set: the machine held in reset (D136) */
} s;

/* the trigger (theory section 8): RAM A addressed by A16-A2, RAM B by A31-A17; bit i of each is pattern i,
 * which matches when both bits are 1. Each half of a pattern is a value under a mask, or a range. */
typedef struct { uint8_t kind; uint16_t a, b; } half_t;        /* kind 0: never, 1: (t ^ a) & b == 0, 2: a <= t <= b */
static struct { int on; half_t lo, hi; char text[40]; } pat[8];
static uint8_t trig_a[NSAMPLE], trig_b[NSAMPLE];
static uint8_t trig_dirty[NSAMPLE / 8];                         /* bit t: trig_a[t] or trig_b[t] not yet written */

/* ---------------------------------------------------------------- replies */
static char out[200];
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out, sizeof out, fmt, ap);
    va_end(ap);
    hal_put(out);
}
static void err(int code, const char *fmt, ...)
{
    va_list ap;
    strcpy(out, "ERR ");
    va_start(ap, fmt);
    vsnprintf(out + 4, sizeof out - 6, fmt, ap);
    va_end(ap);
    strcat(out, "\n");
    hal_put(out);
    s.err = code;
}

/* ---------------------------------------------------------------- the local bus */
static void wr(uint8_t xa, uint8_t v)
{
    hal_wr(xa, v);
    switch (xa) {
    case XA_CTRL0: s.ctrl0 = v; break;
    case XA_CTRL1: s.ctrl1 = v; break;
    case XA_POSTL: s.post = (uint16_t)((s.post & 0xFF00) | v); break;
    case XA_POSTH: s.post = (uint16_t)((s.post & 0x00FF) | v << 8); break;
    case XA_TSL: s.ts = (uint16_t)((s.ts & 0x7F00) | v); break;
    case XA_TSH: s.ts = (uint16_t)((s.ts & 0x00FF) | (v & 0x7F) << 8); s.mres = (v & MRES) != 0; break;
    case XA_DISP: s.disp = v; s.disp_known = 1; break;
    case XA_IRQ: s.irq = v; break;
    default:
        if (xa < NDRIVE) {
            s.drv[xa] = v;
            s.drv_known |= 1u << xa;
        }
    }
}
static uint8_t status(void) { return hal_rd(XA_STATUS); }
static uint16_t ca_now(void)
{
    uint8_t lo = hal_rd(XA_CAL), hi = hal_rd(XA_CAH);
    return (uint16_t)(hi << 8 | lo);
}
static void ctrl0(uint8_t v) { if (v != s.ctrl0) wr(XA_CTRL0, v); }
static void ctrl1(uint8_t v) { if (v != s.ctrl1) wr(XA_CTRL1, v); }
static void drv(int k, uint8_t v)
{
    if (!(s.drv_known >> k & 1) || s.drv[k] != v)
        wr((uint8_t)k, v);
}

/* CRESN low and back: the control registers, GO, ARM, STEP, DBSEQ, DBCAP and the interrupt register clear
 * (theory section 5). XCLKEN goes with CTRL1: a CPU card on XCLK is back on its own oscillator. */
static void card_reset(void)
{
    hal_cresn(0);
    hal_delay_ns(1000);
    hal_cresn(1);
    hal_delay_ns(1000);
    s.ctrl0 = s.ctrl1 = s.irq = 0;
    s.post = s.ts = 0;
    s.mres = 0;                         /* (TSAH cleared: PWROK let go) */
    s.armed = s.have_end = 0;
    s.clk_at3 = 0;                      /* (CLKRUN went: the phase counter stands where it stopped) */
}

/* A GO that never ended (no grant, no clock) is cancelled by a reset; the settings are put back. */
static void restore(void)
{
    uint8_t c0 = s.ctrl0, c1 = s.ctrl1 & (uint8_t)~(TNOW | SETUP), irq = s.irq, mres = s.mres ? MRES : 0;
    uint16_t post = s.post, ts = s.ts;
    card_reset();
    wr(XA_POSTL, post & 0xFF); wr(XA_POSTH, post >> 8);
    wr(XA_TSL, ts & 0xFF); wr(XA_TSH, (uint8_t)(ts >> 8 | mres));
    wr(XA_IRQ, irq);
    wr(XA_CTRL1, c1);
    wr(XA_CTRL0, c0);
}

/* Is the card there, with power? With CRESN high, ARM sets ARMQ, which stays (in DRIVE, or with no sample
 * clock) or starts a capture (ACT): a 1 in the status either way; CRESN low clears both. Only a powered card
 * reads 1 and then 0. (Not CSTEP: the capture address's clock is gated by the SRAMs' -WE, which from power-on
 * hangs on WQ, a flip-flop CRESN does not clear and only a falling sample clock sets: D135.) XD is driven low
 * and the probe lasts a few microseconds: an unpowered card's inputs would take current from a pin driven
 * high (backlog, the review before building, item 5). A capture in the SRAMs is overwritten. */
static int probe(void)
{
    hal_take();
    hal_cresn(1);
    hal_delay_ns(200);
    hal_wr(XA_ARM, 0);
    uint8_t a = hal_rd(XA_STATUS);
    hal_cresn(0);
    hal_delay_ns(200);
    uint8_t b = hal_rd(XA_STATUS);
    if ((a & (S_ARMQ | S_ACT)) && !(b & (S_ARMQ | S_ACT | S_DONE | S_GOQ | S_SNAP)))
        return 1;
    hal_release();
    return 0;
}
/* The card's 5 V (D136): the Pico runs from USB with the machine off, so before anything is driven, before each
 * command and while waiting for one (dbg_idle(), D151), the firmware looks at the card's VCC (halved onto GP26).
 * Off, it lets go of the local bus, CRESN low, and forgets what the card held: after power comes back the card
 * stays clear until the next command, which probes and resets it. (hal_vcc_mv() is -1 where it cannot tell: then
 * the probe alone decides.) */
static int vcc_off(int mv)
{
    if (mv < 0 || mv >= VCC_MIN_MV)
        return 0;
    if (s.alive) {
        hal_release();
        s.alive = 0;
    }
    s.drv_known = 0;                    /* (the drive registers, the trigger RAMs and the display hold anything */
    s.disp_known = 0;                   /*  after power-on) */
    s.trig_valid = 0;
    s.alone_mmu = 0;
    return 1;
}
static int powered(void)
{
    int mv = hal_vcc_mv();
    if (!vcc_off(mv))
        return 1;
    err(E_POWER, "the card's 5 V is %d.%02d V: is the machine's power on?", mv / 1000, mv % 1000 / 10);
    return 0;
}
static int ensure(void)
{
    if (!powered())
        return 0;
    if (s.alive)
        return 1;
    if (probe()) {
        s.alive = 1;
        card_reset();
        return 1;
    }
    err(E_POWER, "the card does not answer: is the machine's power on?");
    return 0;
}

/* nothing armed or running that a live read, a DMA cycle or a read-back would disturb */
static int quiet(void)
{
    if (status() & (S_ACT | S_ARMQ)) {
        err(E_REFUSED, "a capture is armed or running: STOP it first");
        return 0;
    }
    return 1;
}
/* The lines as they stand: LIVE, the latches sampling at 12 MHz (so with no CLK too), then CTRL1 as it was. */
static void live(uint8_t *v, uint32_t mask)
{
    uint8_t c1 = s.ctrl1;
    ctrl1((uint8_t)((c1 & ~(SETUP | TNOW)) | LIVE | SCLKSEL));
    hal_delay_ns(LIVE_NS);
    for (int k = 0; k < NSLICE; k++)
        if (mask >> k & 1)
            v[k] = hal_rd((uint8_t)k);
    ctrl1(c1);
}
static int line_of(const uint8_t *v, uint8_t place) { return v[SL(place)] >> BIT(place) & 1; }
/* Which cards are in (D102: each ties its line to ground), and PWROK. */
static int cards(int *cpu, int *mmu, int *pwrok)
{
    uint8_t v[NSLICE];
    if (!quiet())
        return 0;
    live(v, 1u << SL(L_CPUINN) | 1u << SL(L_MMUINN) | 1u << SL(L_PWROK));
    *cpu = !line_of(v, L_CPUINN);
    *mmu = !line_of(v, L_MMUINN);
    *pwrok = line_of(v, L_PWROK);
    return 1;
}

static int need_drive(void)
{
    if (!(status() & S_DRIVE)) {
        err(E_REFUSED, "the mode switch is at LISTEN: this needs DRIVE");
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- the clocks (theory section 11) */
/* One STEP: alone, DBCLK's counter runs from phase 3 round to phase 3 again, one whole CLK period; with
 * XCLKEN, one XCLK pulse (one of the CPU card's eight phases). hal_wr's strobe spans an oscillator edge and is
 * far shorter than a CLK period (hal.h). */
static void step(void)
{
    wr(XA_STEP, 0);
    hal_delay_ns(STEP_NS);
}
/* Alone: one whole CLK period. From power-on, or wherever CLKRUN left the counter, the first STEP only takes
 * it to phase 3 (theory section 11), so one more is given then. */
static void clk_step(void)
{
    if (!s.clk_at3) {
        step();
        s.clk_at3 = 1;
    }
    step();
}

/* ---------------------------------------------------------------- one bus cycle (theory sections 9, 10) */
/* One cycle by GO: beside a running CPU a DMA cycle (slot 0's -BREQ0, the arbiter's grant); alone, with the
 * bus the card's, ungranted, on the card's clock (stepped here unless CLKRUN). Its end is left in the capture
 * SRAMs at CA (SNAP) and CA moves on by one. Waits until GOQ and SNAP are both clear. 0, or -1: it never
 * ended (no grant, no clock), and the GO has been cancelled. */
static int cycle(uint32_t addr, uint8_t be, int write, uint32_t data)
{
    uint8_t r[9] = {0};
    for (int i = 2; i < 32; i++)
        if (addr >> i & 1)
            r[SL(L_A[i])] |= (uint8_t)(1 << BIT(L_A[i]));
    for (int i = 0; i < 4; i++)
        if (be >> i & 1)
            r[SL(L_BE[i])] |= (uint8_t)(1 << BIT(L_BE[i]));
    r[SL(L_MRDN)] |= (uint8_t)((write ? 1 : 0) << BIT(L_MRDN));
    r[SL(L_MWRN)] |= (uint8_t)((write ? 0 : 1) << BIT(L_MWRN));
    r[SL(L_IACKN)] |= (uint8_t)(1 << BIT(L_IACKN));
    if (write)
        for (int i = 0; i < 32; i++)
            if (data >> i & 1)
                r[SL(L_D[i])] |= (uint8_t)(1 << BIT(L_D[i]));
    for (int k = 4; k <= 8; k++)
        drv(k, r[k]);
    if (write)
        for (int k = 0; k <= 3; k++)
            drv(k, r[k]);
    /* the snapshot is the cycle's end only with the sample clock at CLK */
    ctrl1((uint8_t)((s.ctrl1 & ~(DMAWR | LIVE | SCLKSEL | SETUP | TNOW)) | (write ? DMAWR : 0)));
    wr(XA_GO, 0);
    uint32_t t0 = hal_us();
    for (int n = 0;; n++) {
        if (!(status() & (S_GOQ | S_SNAP))) {
            /* SNAP is over, but the slices show the latches until the sample clock next falls (WQ covers the
             * last write: theory section 8). Stepped, the clock stands high (phase 3, or wherever XCLK left
             * it), so one more period is given; running, it falls within one (D135). */
            if ((s.ctrl0 & ALONE) && !(s.ctrl1 & CLKRUN))
                clk_step();
            else if ((s.ctrl1 & XCLKEN) && !(s.ctrl1 & CLKRUN))
                for (int i = 0; i < 8; i++)
                    step();
            else
                hal_delay_ns(STEP_NS);
            return 0;
        }
        if ((s.ctrl0 & ALONE) && !(s.ctrl1 & CLKRUN)) {
            if (n > 8)                  /* (GO, CYC, SNAP: three periods; a slave's -WAIT may add a few) */
                break;
            clk_step();
        } else if ((s.ctrl1 & XCLKEN) && !(s.ctrl1 & CLKRUN)) {
            if (n > 8 * 12)             /* the machine stepped on XCLK: give it phases, as the host would */
                break;
            step();
        } else if ((uint32_t)(hal_us() - t0) > GO_US)
            break;
    }
    restore();
    return -1;
}

/* the snapshot at CA (LIVE off, nothing running: the slices show the SRAMs): its D, and whether a card
 * answered (-ACK low) */
static uint32_t snap(int *ack)
{
    uint8_t v[NSLICE];
    uint32_t d = 0;
    for (int k = 0; k < 4; k++)
        v[k] = hal_rd((uint8_t)k);
    v[SL(L_ACKN)] = hal_rd(SL(L_ACKN));
    for (int i = 0; i < 32; i++)
        if (line_of(v, L_D[i]))
            d |= 1u << i;
    *ack = !line_of(v, L_ACKN);
    return d;
}

/* A bus cycle may run: DRIVE; and alone, only with the bus the card's (no MMU / data card: D102). */
static int bus_ok(void)
{
    int cpu, mmu, pwrok;
    if (!need_drive() || !quiet())
        return 0;
    if (!cards(&cpu, &mmu, &pwrok))
        return 0;
    if (s.ctrl0 & ALONE) {
        if (cpu) {
            err(E_REFUSED, "the CPU card is in: alone does nothing (D102); ALONE OFF for a DMA cycle beside it");
            return 0;
        }
        if (mmu) {
            err(E_REFUSED, "the MMU / data card is in: the bus cycle's lines are its (D102); ALONE OFF for DMA");
            return 0;
        }
    } else if (s.mres) {                /* (-RES holds the arbiter too, D136) */
        err(E_REFUSED, "the machine is held in reset (MRES ON): its arbiter grants nothing; MRES OFF first");
        return 0;
    } else if (!mmu) {                  /* (the arbiter is the MMU / data card's: without it no grant comes) */
        err(E_REFUSED, "no MMU / data card: nothing grants the bus; ALONE ON to be its master");
        return 0;
    }
    return 1;
}

static void cmd_rd(uint32_t a, uint32_t n)
{
    if (n < 1 || n > NSAMPLE) {
        err(E_SYNTAX, "RD: 1 to 8000 words (hex: 32 K)");
        return;
    }
    if (!bus_ok())
        return;
    a &= ~3u;
    s.armed = s.have_end = 0;           /* (the snapshots go where a capture was) */
    wr(XA_CAPCLR, 0);
    for (uint32_t i = 0; i < n; i++)
        if (cycle(a + 4 * i, 0xF, 0, 0)) {
            err(E_TIME, "RD %08X: the cycle never ended (%s); cancelled", (unsigned)(a + 4 * i),
                (s.ctrl0 & ALONE) ? "no clock" : "no grant: is the MMU / data card in? -LOCK held?");
            return;
        }
    wr(XA_CAPCLR, 0);
    uint32_t bad = 0, first_bad = 0;
    char *p = out;
    for (uint32_t i = 0; i < n; i++) {
        int ack;
        uint32_t d = snap(&ack);
        wr(XA_CSTEP, 0);
        if (i % 8 == 0)
            p = out + sprintf(out, "%08X:", (unsigned)(a + 4 * i));
        if (ack)
            p += sprintf(p, " %08X", (unsigned)d);
        else {
            p += sprintf(p, " --------");
            if (!bad++)
                first_bad = a + 4 * i;
        }
        if (i % 8 == 7 || i == n - 1) {
            strcpy(p, "\n");
            hal_put(out);
        }
    }
    if (bad)
        err(E_NOACK, "%X of %X words had no -ACK (nothing answered), the first at %08X",
            (unsigned)bad, (unsigned)n, (unsigned)first_bad);
    else
        say("OK %X\n", (unsigned)n);
}

/* writes, then the snapshots' -ACK read back */
static void writes(uint32_t a, const uint32_t *w, uint32_t n, uint8_t be)
{
    if (!bus_ok())
        return;
    s.armed = s.have_end = 0;
    wr(XA_CAPCLR, 0);
    for (uint32_t i = 0; i < n; i++)
        if (cycle(a + 4 * i, be, 1, w[i])) {
            err(E_TIME, "WR %08X: the cycle never ended (%s); cancelled", (unsigned)(a + 4 * i),
                (s.ctrl0 & ALONE) ? "no clock" : "no grant: is the MMU / data card in? -LOCK held?");
            return;
        }
    wr(XA_CAPCLR, 0);
    uint32_t bad = 0, first_bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t v[NSLICE];
        v[SL(L_ACKN)] = hal_rd(SL(L_ACKN));
        wr(XA_CSTEP, 0);
        if (line_of(v, L_ACKN) && !bad++)
            first_bad = a + 4 * i;
    }
    if (bad)
        err(E_NOACK, "%X of %X writes had no -ACK (nothing answered), the first at %08X",
            (unsigned)bad, (unsigned)n, (unsigned)first_bad);
    else
        say("OK %X\n", (unsigned)n);
}

/* ---------------------------------------------------------------- the trigger RAMs (theory section 8) */
static int half_match(const half_t *h, uint16_t t)
{
    return h->kind == 1 ? ((t ^ h->a) & h->b) == 0 : h->kind == 2 ? (t >= h->a && t <= h->b) : 0;
}
static void trig_compute(int i)
{
    uint8_t bit = (uint8_t)(1 << i);
    for (uint32_t t = 0; t < NSAMPLE; t++) {
        uint8_t a = (uint8_t)((trig_a[t] & ~bit) | (pat[i].on && half_match(&pat[i].lo, (uint16_t)t) ? bit : 0));
        uint8_t b = (uint8_t)((trig_b[t] & ~bit) | (pat[i].on && half_match(&pat[i].hi, (uint16_t)t) ? bit : 0));
        if (a != trig_a[t] || b != trig_b[t]) {
            trig_a[t] = a;
            trig_b[t] = b;
            trig_dirty[t >> 3] |= (uint8_t)(1 << (t & 7));
        }
    }
}
/* Write the trigger RAMs: all of both the first time (from power-on they hold anything: theory section 15,
 * item 3), then only what changed. SETUP: the RAMs' address from TS, XD onto their data pins, no trigger. */
static void trig_write(void)
{
    int all = !s.trig_valid, any = all;
    for (uint32_t i = 0; i < sizeof trig_dirty && !any; i++)
        any = trig_dirty[i] != 0;
    if (!any)
        return;                         /* nothing to write */
    ctrl1((uint8_t)((s.ctrl1 & ~(TNOW | LIVE)) | SETUP));
    for (uint32_t t = 0; t < NSAMPLE; t++) {
        if (!all && !(trig_dirty[t >> 3] >> (t & 7) & 1))
            continue;
        if ((s.ts & 0xFF) != (t & 0xFF))
            wr(XA_TSL, (uint8_t)(t & 0xFF));
        if ((s.ts >> 8) != (t >> 8))
            wr(XA_TSH, (uint8_t)(t >> 8 | (s.mres ? MRES : 0)));
        wr(XA_TRA, trig_a[t]);
        wr(XA_TRB, trig_b[t]);
    }
    memset(trig_dirty, 0, sizeof trig_dirty);
    s.trig_valid = 1;
    ctrl1((uint8_t)(s.ctrl1 & ~SETUP));
}

/* TRIG n ADDR a [mask], TRIG n RANGE a b, TRIG n OFF, TRIG CLR, TRIG INIT, TRIG ZERO, TRIG (the list) */
static void cmd_trig(int argc, char **argv, uint32_t *num, int *isnum)
{
    if (argc == 1) {
        for (int i = 0; i < 8; i++)
            if (pat[i].on)
                say("%d %s\n", i, pat[i].text);
        say("OK the trigger RAMs %s\n", s.trig_valid ? "written" : "not yet written (at the next ARM, or TRIG INIT)");
        return;
    }
    if (!strcmp(argv[1], "CLR")) {
        for (int i = 0; i < 8; i++)
            if (pat[i].on) {
                pat[i].on = 0;
                trig_compute(i);
            }
        say("OK\n");
        return;
    }
    if (!strcmp(argv[1], "INIT")) {     /* write both RAMs whole, now */
        s.trig_valid = 0;
        trig_write();
        say("OK\n");
        return;
    }
    if (!strcmp(argv[1], "ZERO")) {     /* for the simulator, whose RAMs start at 0: never needed on a card */
        memset(trig_a, 0, sizeof trig_a);
        memset(trig_b, 0, sizeof trig_b);
        memset(trig_dirty, 0, sizeof trig_dirty);
        for (int i = 0; i < 8; i++)
            pat[i].on = 0;
        s.trig_valid = 1;
        say("OK the trigger RAMs taken as all 0\n");
        return;
    }
    if (!isnum[1] || num[1] > 7 || argc < 3) {
        err(E_SYNTAX, "TRIG n ADDR a [mask] | n RANGE a b | n OFF | CLR | INIT (n 0-7)");
        return;
    }
    int i = (int)num[1];
    if (!strcmp(argv[2], "OFF")) {
        pat[i].on = 0;
    } else if (!strcmp(argv[2], "ADDR") && argc >= 4 && isnum[3]) {
        uint32_t a = num[3], m = argc >= 5 && isnum[4] ? num[4] : 0xFFFFFFFCu;
        pat[i].lo = (half_t){1, (uint16_t)(a >> 2 & 0x7FFF), (uint16_t)(m >> 2 & 0x7FFF)};
        pat[i].hi = (half_t){1, (uint16_t)(a >> 17), (uint16_t)(m >> 17)};
        snprintf(pat[i].text, sizeof pat[i].text, "ADDR %08X %08X", (unsigned)a, (unsigned)m);
        pat[i].on = 1;
    } else if (!strcmp(argv[2], "RANGE") && argc >= 5 && isnum[3] && isnum[4] && num[3] <= num[4]) {
        uint32_t a = num[3], b = num[4];
        if (a >> 17 == b >> 17) {       /* within one 128 KiB block: a range of low halves, the high half fixed */
            pat[i].lo = (half_t){2, (uint16_t)(a >> 2 & 0x7FFF), (uint16_t)(b >> 2 & 0x7FFF)};
            pat[i].hi = (half_t){1, (uint16_t)(a >> 17), 0x7FFF};
        } else if ((a & 0x1FFFF) == 0 && (b & 0x1FFFF) == 0x1FFFF) {     /* whole blocks */
            pat[i].lo = (half_t){1, 0, 0};
            pat[i].hi = (half_t){2, (uint16_t)(a >> 17), (uint16_t)(b >> 17)};
        } else {
            err(E_SYNTAX, "a RANGE lies within one 128 KiB block, or is whole blocks (two patterns can join them)");
            return;
        }
        snprintf(pat[i].text, sizeof pat[i].text, "RANGE %08X %08X", (unsigned)a, (unsigned)b);
        pat[i].on = 1;
    } else {
        err(E_SYNTAX, "TRIG n ADDR a [mask] | n RANGE a b | n OFF");
        return;
    }
    trig_compute(i);
    say("OK\n");
}

/* ---------------------------------------------------------------- the capture (theory section 8) */
static void cmd_arm(int fast, uint32_t n)
{
    if (n < 1 || n > 65536) {
        err(E_SYNTAX, "ARM [12M] [n]: n post-trigger samples, 1 to 10000 (hex)");
        return;
    }
    uint8_t st = status();
    if (st & S_DRIVE) {
        err(E_REFUSED, "the mode switch is at DRIVE: a capture needs LISTEN (D64)");
        return;
    }
    if (st & (S_ACT | S_ARMQ)) {
        err(E_REFUSED, "a capture is armed or running: STOP it first");
        return;
    }
    trig_write();
    /* SETUP through ARM's strobe: no trigger can come while ARMQ's preset is held, which would outlast
     * ARMCLRN (RUN ended by an early trigger) and arm a second capture over the first (D135). Once the strobe
     * is over the preset is gone, so SETUP goes at once: a trigger is missed only in the sample or two of
     * those two writes. */
    ctrl1((uint8_t)((s.ctrl1 & (ODLOCK | CLKRUN | XCLKEN)) | SETUP | (fast ? SCLKSEL : 0)));
    uint16_t p = (uint16_t)(65536u - n);
    wr(XA_POSTL, p & 0xFF);
    wr(XA_POSTH, (uint8_t)(p >> 8));
    wr(XA_CAPCLR, 0);
    wr(XA_ARM, 0);
    ctrl1((uint8_t)(s.ctrl1 & ~SETUP));
    s.armed = 1;
    s.have_end = 0;
    say("OK armed: %X samples after the trigger, sampling at %s\n", (unsigned)n, fast ? "12 MHz" : "CLK");
}

/* After a capture, its last write lasts until the sample clock falls (WQ: theory section 8), and only then
 * does CA step past it; a clock that stopped high (the machine stepped on XCLK) leaves it open, and CSTEP
 * cannot count meanwhile (the counter's clock is gated by -WE). The 12 MHz sample clock for a moment ends it:
 * T12 runs whatever else stands still, and with nothing captured no other write follows (D135). */
static void flush(void)
{
    if (!(s.ctrl1 & SCLKSEL)) {
        uint8_t c1 = s.ctrl1;
        ctrl1((uint8_t)(c1 | SCLKSEL));
        hal_delay_ns(200);
        ctrl1(c1);
    }
}

static void note_end(uint8_t st)
{
    if (s.armed && !s.have_end && (st & S_DONE) && !(st & (S_ACT | S_ARMQ))) {
        flush();
        s.end = ca_now();
        s.have_end = 1;
    }
}

static void cmd_cap(void)
{
    uint8_t st = status();
    note_end(st);
    uint16_t ca = s.have_end ? s.end : ca_now();
    say("OK armed=%d running=%d done=%d trig=%d ca=%04X wrapped=%d\n", !!(st & S_ARMQ), !!(st & S_ACT),
        !!(st & S_DONE), !!(st & S_TRIG), ca & 0x7FFF, ca >> 15);
}

static void cmd_stop(void)
{
    uint8_t st = status();
    if (!(st & (S_ACT | S_ARMQ))) {
        note_end(st);
        if (s.have_end)
            say("OK done: ca=%04X wrapped=%d\n", s.end & 0x7FFF, s.end >> 15);
        else
            err(E_REFUSED, "nothing armed");
        return;
    }
    if (!(st & S_ACT)) {                /* armed, and no sample edge has come */
        restore();
        s.armed = 0;
        err(E_TIME, "armed, but no sample was taken: is the sample clock running? (disarmed)");
        return;
    }
    ctrl1((uint8_t)(s.ctrl1 | TNOW));
    uint32_t t0 = hal_us(), phases = 0;
    int stepped = (s.ctrl1 & XCLKEN) && !(s.ctrl1 & CLKRUN) && !(s.ctrl1 & SCLKSEL);
    while (!((st = status()) & S_DONE)) {
        if (stepped) {                  /* the machine stepped on XCLK: the samples are its CLKs, so give phases */
            if (phases++ > 8u * (65536u - s.post + 2))
                break;
            step();
        } else if ((uint32_t)(hal_us() - t0) >= DONE_US)
            break;
    }
    ctrl1((uint8_t)(s.ctrl1 & ~TNOW));
    if (!(st & S_DONE)) {
        err(E_TIME, "no DONE: the sample clock has stopped?");
        return;
    }
    note_end(st);
    say("OK done: ca=%04X wrapped=%d\n", s.end & 0x7FFF, s.end >> 15);
}

/* DUMP [slices] [first] [count]: the capture SRAMs read back, a sample a line: its address, then a byte for
 * each slice asked for (bit k of slices: slice k). By default the samples written: 0 to CA - 1, or all 32 K
 * once the buffer has gone round (the oldest is then at CA). */
static void cmd_dump(uint32_t mask, int have_first, uint32_t first, int have_count, uint32_t count)
{
    uint8_t st = status();
    if (st & (S_ACT | S_ARMQ)) {
        err(E_REFUSED, "a capture is armed or running: STOP it first");
        return;
    }
    note_end(st);
    uint16_t end = s.have_end ? s.end : ca_now();
    int wrapped = end >> 15;
    if (!have_first)
        first = 0;
    if (!have_count)
        count = wrapped ? NSAMPLE - first : ((end & 0x7FFF) > first ? (end & 0x7FFF) - first : 0);
    if (first >= NSAMPLE || count > NSAMPLE - first || !(mask &= (1u << NSLICE) - 1)) {
        err(E_SYNTAX, "DUMP [slices] [first] [count]: first + count at most 8000 (hex), slices a mask of 0-21");
        return;
    }
    ctrl1((uint8_t)(s.ctrl1 & ~(LIVE | SETUP | TNOW)));
    say("CAP end=%04X wrapped=%d first=%04X count=%X slices=%06X\n", end & 0x7FFF, wrapped, (unsigned)first,
        (unsigned)count, (unsigned)mask);
    flush();                            /* (no write left open: CSTEP must count) */
    wr(XA_CAPCLR, 0);
    for (uint32_t i = 0; i < first; i++)
        wr(XA_CSTEP, 0);
    for (uint32_t i = 0; i < count; i++) {
        char *p = out + sprintf(out, "%04X:", (unsigned)(first + i));
        for (int k = 0; k < NSLICE; k++)
            if (mask >> k & 1)
                p += sprintf(p, "%02X", hal_rd((uint8_t)k));
        strcpy(p, "\n");
        hal_put(out);
        wr(XA_CSTEP, 0);
    }
    say("OK %X\n", (unsigned)count);
}

/* ---------------------------------------------------------------- alone, and XCLK */
static void cmd_alone(int on)
{
    if (!on) {
        ctrl0((uint8_t)(s.ctrl0 & (ODACK | ODWAIT)));
        if (!(s.ctrl1 & XCLKEN))
            ctrl1((uint8_t)(s.ctrl1 & ~CLKRUN));
        say("OK%s\n", s.alone_mmu ? ": J3 and R undriven again, as at power-on (no CPU card)" : "");
        s.alone_mmu = 0;
        return;
    }
    int cpu, mmu, pwrok;
    if (!need_drive() || !cards(&cpu, &mmu, &pwrok))
        return;
    if (cpu) {
        err(E_REFUSED, "the CPU card is in: the card refuses ALONE by wiring (D102)");
        return;
    }
    /* the system lines first, all released: -RES, -BREQ3-10, -FAST high, no grant (theory section 12) */
    drv(SL(L_RESN), 0xFF);
    drv(SL(L_FASTN), 0xFF);
    drv(SL(L_BGNT3N), 0xFF);
    /* With the CPU card out, nothing else drives its side of the local link, and J3 has no pull-ups: an MMU /
     * data card's RSRC decode would read floating inputs and might put MAR or a load-steering byte on R. So
     * J3 is driven idle first (RSRC, RDST and MEM none, -WSTB high, CCLK and the rest low: the MMU / data card
     * drives nothing on R), and then, in a write of its own so the two cannot overlap, R (LR) as 0 (backlog,
     * the review before building, item 4; D135). To read what that card puts on R, the host lets LR go (GRP). */
    for (int i = 0; i < 32; i += 8)
        drv(SL(L_LR[i]), 0x00);
    drv(SL(L_RSRC0), 0x00);
    drv(SL(L_MEM1), 0x00);
    drv(SL(L_CCLK), (uint8_t)(1 << BIT(L_WSTBN)));
    s.alone_mmu = mmu;
    ctrl0((uint8_t)((s.ctrl0 & (ODACK | ODWAIT)) | ALONE | GSYS | GJ3));
    ctrl0((uint8_t)(s.ctrl0 | GLR));
    if (!(s.ctrl1 & CLKRUN))
        clk_step();                     /* (to phase 3, and one whole period: CLK stands high) */
    say("OK alone: the card makes CLK; LR and J3 driven, idle%s\n",
        mmu ? "; the MMU / data card is in, so D, the bus cycle and the grants are its" : "");
}

/* Alone with the MMU / data card in (D135): J3 stays driven, for nothing else drives it, it has no pull-ups,
 * and that card decodes it; and LR is driven only while J3's RSRC puts none of that card's registers on R
 * (slices.h, from card_mmu.py), or the two would fight on R. The state asked for: CTRL0 c0, and drive
 * register k (if k >= 0) = v. */
static int alone_ok(uint8_t c0, int k, uint8_t v)
{
    if (!(c0 & ALONE) || !s.alone_mmu)
        return 1;
    if (!(c0 & GJ3)) {
        err(E_REFUSED, "J3 stays driven alone with the MMU / data card in: no pull-ups, and that card decodes it");
        return 0;
    }
    if (!(c0 & GLR))
        return 1;
    uint8_t d[NDRIVE];
    memcpy(d, s.drv, sizeof d);
    if (k >= 0 && k < NDRIVE)
        d[k] = v;
    int rsrc = line_of(d, L_RSRC0) | line_of(d, L_RSRC1) << 1 | line_of(d, L_RSRC2) << 2 | line_of(d, L_RSRC3) << 3;
    int crsel = line_of(d, L_CRSEL0) | line_of(d, L_CRSEL1) << 1 | line_of(d, L_CRSEL2) << 2;
    if ((MMU_RSRC >> rsrc & 1) || ((MMU_CRHW_RSRC >> rsrc & 1) && (MMU_CRHW >> crsel & 1))) {
        err(E_REFUSED, "RSRC %X puts the MMU / data card on R, and LR is driven: they would fight (GRP without GLR "
            "to read R; RSRC 0 before driving LR)", rsrc);
        return 0;
    }
    return 1;
}

/* The machine's reset (D136): TSAH's bit 7 pulls PWROK low through a spare 7406 gate, in DRIVE only (the mode
 * switch gates it), and the CPU card holds RESET while PWROK is low: -RES falls at its clock's next edge (on
 * XCLK, stopped, at the next XSTEP) and rises again after one once PWROK is let go. CRESN lets it go too. */
static void cmd_mres(int argc, char **argv)
{
    int on = argc >= 2 && !strcmp(argv[1], "ON");
    if (argc < 2 || (!on && strcmp(argv[1], "OFF"))) {
        err(E_SYNTAX, "MRES ON|OFF");
        return;
    }
    if (on && !need_drive())
        return;
    wr(XA_TSH, (uint8_t)(s.ts >> 8 | (on ? MRES : 0)));
    uint8_t v[NSLICE];
    if (!(status() & (S_ACT | S_ARMQ))) {           /* (what PWROK and -RES do now, if a live read disturbs nothing) */
        live(v, 1u << SL(L_PWROK) | 1u << SL(L_RESN));
        say("OK PWROK %s, -RES %s%s\n", line_of(v, L_PWROK) ? "high" : "low", line_of(v, L_RESN) ? "high" : "low",
            on && line_of(v, L_RESN) ? " (the CPU card takes PWROK at its clock's next edge; on XCLK, XSTEP)" : "");
    } else
        say("OK\n");
}

static void cmd_clk(int argc, char **argv, uint32_t *num, int *isnum)
{
    if (!(s.ctrl0 & ALONE)) {
        err(E_REFUSED, "not alone: ALONE ON first (with a CPU card in, CLK is its)");
        return;
    }
    if (argc >= 2 && !strcmp(argv[1], "RUN")) {
        ctrl1((uint8_t)(s.ctrl1 | CLKRUN));
        s.clk_at3 = 0;
        say("OK CLK runs: 3 MHz\n");
    } else if (argc >= 2 && !strcmp(argv[1], "STOP")) {
        ctrl1((uint8_t)(s.ctrl1 & ~CLKRUN));
        s.clk_at3 = 0;
        say("OK\n");
    } else if (argc >= 2 && !strcmp(argv[1], "STEP")) {
        uint32_t n = argc >= 3 && isnum[2] ? num[2] : 1;
        if (s.ctrl1 & CLKRUN) {
            err(E_REFUSED, "CLK runs: CLK STOP first");
            return;
        }
        for (uint32_t i = 0; i < n; i++)
            clk_step();
        say("OK %X\n", (unsigned)n);
    } else
        err(E_SYNTAX, "CLK STEP [n] | RUN | STOP");
}

/* XCLK ON|OFF [FORCE]: the CPU card on the card's XCLK (D76). Changed only while -RES is low (someone holds
 * the CPU card's RESET: the card cannot drive -RES with a CPU card in), since CKSEL is a plain multiplexer and
 * a change mid-phase can cut a phase short (theory section 11). -RES is GAL PANEL's RESET register, clocked by
 * CKSEL's choice: on XCLK, stopped, it shows the button only after an edge, so one XCLK pulse is given first
 * (one phase, the machine held in reset by then; D135). */
static void cmd_xclk(int argc, char **argv)
{
    int on = argc >= 2 && !strcmp(argv[1], "ON"), force = argc >= 3 && !strcmp(argv[2], "FORCE");
    if (argc < 2 || (!on && strcmp(argv[1], "OFF"))) {
        err(E_SYNTAX, "XCLK ON|OFF [FORCE]");
        return;
    }
    if (!force) {
        uint8_t v[NSLICE];
        if (!quiet())
            return;
        if ((s.ctrl1 & XCLKEN) && !(s.ctrl1 & CLKRUN))
            step();
        live(v, 1u << SL(L_RESN));
        if (line_of(v, L_RESN)) {
            err(E_REFUSED, "-RES is high: hold the CPU card's RESET (MRES ON, or its button) while its clock is "
                "changed (D76), or FORCE");
            return;
        }
    }
    ctrl1(on ? (uint8_t)(s.ctrl1 | XCLKEN) : (uint8_t)(s.ctrl1 & ~(XCLKEN | CLKRUN)));
    say("OK the CPU card on %s\n", on ? "the debug card's XCLK" : "its own oscillator");
}

/* ---------------------------------------------------------------- the display (D100) */
static void show(void)
{
    if (s.disp_host || !s.alive)
        return;
    uint8_t st = status(), d;
    if (st & S_ACT)
        d = D_CAPTURING;
    else if (st & S_ARMQ)
        d = D_ARMED;
    else if (s.armed && (st & S_DONE))
        d = D_DONE;
    else if (s.ctrl1 & XCLKEN)
        d = D_XCLK;
    else if (s.ctrl0 & ALONE)
        d = D_ALONE;
    else
        d = D_IDLE;
    d = (uint8_t)(d << 4 | (s.err & 0xF));
    if (!s.disp_known || d != s.disp)
        wr(XA_DISP, d);
}

/* ---------------------------------------------------------------- commands */
static const char *const HELP[] = {
    "(numbers are hex)",
    "ID                       the firmware",
    "PROBE                    is the card there, with power?",
    "RESET                    CRESN low and back: every control register 0",
    "ST                       the status register, CTRL0, CTRL1, CA",
    "CARDS                    which cards are in (-CPUIN, -MMUIN), PWROK",
    "LIVE [slices]            the lines as they stand (the latches at 12 MHz)",
    "RD a [n]                 n words read by DMA (DRIVE)",
    "WR a w [w ...]           words written by DMA (DRIVE)",
    "WRB a b, WRH a h         a byte, a halfword",
    "TRIG n ADDR a [mask]     pattern n (0-7): a matching address (mask 1s compared)",
    "TRIG n RANGE a b         pattern n: a to b, within one 128 KiB block or whole blocks",
    "TRIG n OFF | CLR | INIT  a pattern off; all off; both RAMs written now",
    "ARM [12M] [n]            capture (LISTEN), n samples kept after the trigger (1)",
    "STOP                     trigger now and wait for the end",
    "CAP                      the capture's state",
    "DUMP [slices] [first] [count]   the samples, a line each",
    "ALONE ON|OFF             the card makes CLK (DRIVE, no CPU card)",
    "CLK STEP [n] | RUN | STOP   alone: CLK a period at a time, or 3 MHz",
    "RES ON|OFF, FAST ON|OFF  alone: -RES, -FAST low or released",
    "XCLK ON|OFF [FORCE]      the CPU card on the card's XCLK (hold its RESET)",
    "MRES ON|OFF              the machine held in reset: PWROK pulled low (DRIVE)",
    "XSTEP [n], XRUN, XSTOP   on XCLK: n of the CPU's phases, or 1.5 MHz",
    "IRQ v                    -IRQ7-0: a 1 pulls that line (DRIVE)",
    "OD ack wait lock         -ACK, -WAIT, -LOCK pulled low (1) (DRIVE)",
    "DRV k v, GRP v           a drive register; the groups GD GBUS GSYS GLR GJ3 (bits 0-4)",
    "                         (alone with the MMU card: J3 kept driven; LR not against its R)",
    "DISP v | AUTO            the status display",
    "W xa v, R xa             one local-bus write or read",
    0};

static int hexnum(const char *t, uint32_t *v)
{
    uint32_t x = 0;
    int n = 0;
    for (; *t; t++, n++) {
        int c = *t;
        if (c >= '0' && c <= '9')
            c -= '0';
        else if (c >= 'A' && c <= 'F')
            c -= 'A' - 10;
        else
            return 0;
        if (n == 8)
            return 0;
        x = x << 4 | (uint32_t)c;
    }
    *v = x;
    return n > 0;
}

#define MAXARG 72
static void run(int argc, char **argv, uint32_t *num, int *isnum)
{
    const char *c = argv[0];
    if (!strcmp(c, "ID")) {
        say("OK P32W debug card firmware %s\n", DBG_VERSION);
        return;
    }
    if (!strcmp(c, "HELP")) {
        for (int i = 0; HELP[i]; i++)
            say("%s\n", HELP[i]);
        say("OK\n");
        return;
    }
    if (!strcmp(c, "PROBE")) {
        s.alive = 0;
        if (ensure())
            say("OK the card answers\n");
        return;
    }
    if (!ensure())
        return;
    if (!strcmp(c, "RESET")) {
        card_reset();
        s.disp_host = 0;
        say("OK\n");
    } else if (!strcmp(c, "ST")) {
        uint8_t st = status();
        uint16_t ca = ca_now();
        say("OK status=%02X drive=%d granted=%d goq=%d running=%d done=%d armed=%d trig=%d snap=%d "
            "ctrl0=%02X ctrl1=%02X ca=%04X wrapped=%d mres=%d\n", st, st & 1, !(st >> 1 & 1), st >> 2 & 1,
            st >> 3 & 1, st >> 4 & 1, st >> 5 & 1, st >> 6 & 1, st >> 7 & 1, s.ctrl0, s.ctrl1, ca & 0x7FFF, ca >> 15,
            s.mres);
    } else if (!strcmp(c, "CARDS")) {
        int cpu, mmu, pwrok;
        if (cards(&cpu, &mmu, &pwrok)) {
            int mv = hal_vcc_mv();
            char vcc[24] = "";
            if (mv >= 0)
                snprintf(vcc, sizeof vcc, " vcc=%d.%02dV", mv / 1000, mv % 1000 / 10);
            say("OK cpu=%s mmu=%s pwrok=%d%s\n", cpu ? "in" : "out", mmu ? "in" : "out", pwrok, vcc);
        }
    } else if (!strcmp(c, "LIVE")) {
        uint32_t mask = argc >= 2 && isnum[1] ? num[1] : (1u << NSLICE) - 1;
        uint8_t v[NSLICE];
        if (quiet()) {
            live(v, mask);
            char *p = out + sprintf(out, "OK");
            for (int k = 0; k < NSLICE; k++)
                if (mask >> k & 1)
                    p += sprintf(p, " %02X", v[k]);
            strcpy(p, "\n");
            hal_put(out);
        }
    } else if (!strcmp(c, "RD") && argc >= 2 && isnum[1]) {
        cmd_rd(num[1], argc >= 3 && isnum[2] ? num[2] : 1);
    } else if (!strcmp(c, "WR") && argc >= 3) {
        static uint32_t w[MAXARG];
        int n = 0;
        for (int i = 2; i < argc; i++) {
            if (!isnum[i]) {
                err(E_SYNTAX, "WR a w [w ...]: hex words");
                return;
            }
            w[n++] = num[i];
        }
        if (isnum[1])
            writes(num[1] & ~3u, w, (uint32_t)n, 0xF);
    } else if ((!strcmp(c, "WRB") || !strcmp(c, "WRH")) && argc >= 3 && isnum[1] && isnum[2]) {
        uint32_t a = num[1], v;
        uint8_t be;
        if (c[2] == 'B') {
            be = (uint8_t)(1 << (a & 3));
            v = (num[2] & 0xFF) << 8 * (a & 3);
        } else {
            be = (uint8_t)(3 << (a & 2));
            v = (num[2] & 0xFFFF) << 8 * (a & 2);
        }
        writes(a & ~3u, &v, 1, be);
    } else if (!strcmp(c, "TRIG")) {
        cmd_trig(argc, argv, num, isnum);
    } else if (!strcmp(c, "ARM")) {
        int fast = argc >= 2 && !strcmp(argv[1], "12M"), k = fast ? 2 : 1;
        cmd_arm(fast, argc > k && isnum[k] ? num[k] : 1);
    } else if (!strcmp(c, "STOP")) {
        cmd_stop();
    } else if (!strcmp(c, "CAP")) {
        cmd_cap();
    } else if (!strcmp(c, "DUMP")) {
        cmd_dump(argc >= 2 && isnum[1] ? num[1] : (1u << NSLICE) - 1, argc >= 3 && isnum[2], argc >= 3 ? num[2] : 0,
                 argc >= 4 && isnum[3], argc >= 4 ? num[3] : 0);
    } else if (!strcmp(c, "ALONE") && argc >= 2) {
        cmd_alone(!strcmp(argv[1], "ON"));
    } else if (!strcmp(c, "CLK")) {
        cmd_clk(argc, argv, num, isnum);
    } else if ((!strcmp(c, "RES") || !strcmp(c, "FAST")) && argc >= 2) {
        if (!(s.ctrl0 & ALONE)) {
            err(E_REFUSED, "not alone: ALONE ON first");
            return;
        }
        uint8_t place = c[0] == 'R' ? L_RESN : L_FASTN;
        uint8_t v = s.drv[SL(place)];
        v = !strcmp(argv[1], "ON") ? (uint8_t)(v & ~(1 << BIT(place))) : (uint8_t)(v | 1 << BIT(place));
        drv(SL(place), v);
        say("OK\n");
    } else if (!strcmp(c, "XCLK")) {
        cmd_xclk(argc, argv);
    } else if (!strcmp(c, "MRES")) {
        cmd_mres(argc, argv);
    } else if (!strcmp(c, "XSTEP") || !strcmp(c, "XRUN") || !strcmp(c, "XSTOP")) {
        if (!(s.ctrl1 & XCLKEN)) {
            err(E_REFUSED, "XCLK ON first");
            return;
        }
        if (c[1] == 'R')
            ctrl1((uint8_t)(s.ctrl1 | CLKRUN));
        else if (c[1] == 'S' && c[2] == 'T' && c[3] == 'O')
            ctrl1((uint8_t)(s.ctrl1 & ~CLKRUN));
        else {
            uint32_t n = argc >= 2 && isnum[1] ? num[1] : 1;
            if (s.ctrl1 & CLKRUN) {
                err(E_REFUSED, "XCLK runs: XSTOP first");
                return;
            }
            for (uint32_t i = 0; i < n; i++)
                step();
        }
        say("OK\n");
    } else if (!strcmp(c, "IRQ") && argc >= 2 && isnum[1]) {
        wr(XA_IRQ, (uint8_t)num[1]);
        say("OK%s\n", (status() & S_DRIVE) ? "" : " (the switch is at LISTEN: the register is held clear)");
    } else if (!strcmp(c, "OD") && argc >= 4 && isnum[1] && isnum[2] && isnum[3]) {
        ctrl0((uint8_t)((s.ctrl0 & ~(ODACK | ODWAIT)) | (num[1] ? ODACK : 0) | (num[2] ? ODWAIT : 0)));
        ctrl1((uint8_t)((s.ctrl1 & ~ODLOCK) | (num[3] ? ODLOCK : 0)));
        say("OK\n");
    } else if (!strcmp(c, "DRV") && argc >= 3 && isnum[1] && isnum[2] && num[1] < NDRIVE) {
        if (alone_ok(s.ctrl0, (int)num[1], (uint8_t)num[2])) {
            drv((int)num[1], (uint8_t)num[2]);
            say("OK\n");
        }
    } else if (!strcmp(c, "GRP") && argc >= 2 && isnum[1]) {
        uint8_t c0 = (uint8_t)((s.ctrl0 & ~GROUPS) | (num[1] & GROUPS));
        if (alone_ok(c0, -1, 0)) {
            ctrl0(c0);
            say("OK\n");
        }
    } else if (!strcmp(c, "DISP") && argc >= 2) {
        if (!strcmp(argv[1], "AUTO"))
            s.disp_host = 0;
        else if (isnum[1]) {
            s.disp_host = 1;
            wr(XA_DISP, (uint8_t)num[1]);
        }
        say("OK\n");
    } else if (!strcmp(c, "W") && argc >= 3 && isnum[1] && isnum[2] && num[1] < 0x80) {
        wr((uint8_t)num[1], (uint8_t)num[2]);
        say("OK\n");
    } else if (!strcmp(c, "R") && argc >= 2 && isnum[1] && num[1] < 0x80) {
        say("OK %02X\n", hal_rd((uint8_t)num[1]));
    } else
        err(E_SYNTAX, "what? (HELP lists the commands)");
}

void dbg_line(char *line)
{
    char *argv[MAXARG];
    uint32_t num[MAXARG];
    int isnum[MAXARG], argc = 0;
    for (char *p = line; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    for (char *p = strtok(line, " \t\r\n"); p && argc < MAXARG; p = strtok(0, " \t\r\n")) {
        argv[argc] = p;
        isnum[argc] = hexnum(p, &num[argc]);
        argc++;
    }
    if (!argc)
        return;
    s.err = E_OK;
    run(argc, argv, num, isnum);
    show();
}

/* Waiting for a command (D151). Without this, a machine switched off and on again with no command between came
 * up with CRESN still driven high by the Pico, and nothing cleared the card's registers. */
void dbg_idle(void)
{
    vcc_off(hal_vcc_mv());
}

void dbg_init(void)
{
    memset(&s, 0, sizeof s);
    hal_release();
    if (probe()) {
        s.alive = 1;
        card_reset();
        show();
    }
}
