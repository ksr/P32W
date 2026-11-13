/*
 * p32io.h -- P32W's devices as both emulators see them: one copy of every
 * device, included by p32emu.c (instruction level) and p32uemu.c (microcode
 * level), so the two cannot drift apart. The cards and their registers:
 * docs/architecture.md, docs/programmers-guide.md.
 *
 * Every card sits in a 4 KiB window chosen by its jumpers (D52), window n at
 * FFFF_8000 + n * 1000, its registers in the window's top 256 bytes. The
 * default jumpers, as here: the I/O card window 7, the disk card window 6,
 * the video card window 5.
 *
 * The I/O card (D50), window 7:
 *   FFFF_FF00-07  UART 0, TL16C550 registers 0-7 (console: stdin/stdout)  IRQ2
 *   FFFF_FF08-0F  UART 1 (-a / -A files, or -T a TCP port)                 IRQ3
 *   FFFF_FF10     TCOUNT  \
 *   FFFF_FF14     TRELOAD  } timer, 32-bit words (isa-system.md B4)        IRQ0
 *   FFFF_FF18     TCTRL   /
 *   FFFF_FF20     input port (8 bits, read; -p VALUE)
 *   FFFF_FF21     output port (8 bits, write, reads back)
 *   FFFF_FF30-37  UART 2 (-a2 / -A2 / -T2)                                 IRQ2
 *   FFFF_FF38-3F  UART 3 (-a3 / -A3 / -T3)                                 IRQ3
 *   FFFF_FF40     real-time clock: the register number (write)  \  D23: a DS12C887
 *   FFFF_FF41     real-time clock: the register (read / write)  /  (MC146818)    IRQ5
 *   FFFF_FF50     USB 0 (CH376S, sector mode): data (read / write) \  -U IMAGE
 *   FFFF_FF51     USB 0: command (write) / status (read)           /           IRQ6
 *   FFFF_FF52     network coprocessor (D45): data (read / write) \  -N
 *   FFFF_FF53     network: status (read) / control (write)        /          IRQ7
 *   FFFF_FF54/55  USB 1, as FF50/51 (-U1 IMAGE)                               IRQ6
 * The disk card (D50), window 6: four ATA ports, 16 bytes each, port p at
 * FFFF_EF00 + 16 p (0 the boot CompactFlash card, -d; 1 the second CF
 * socket, -d1; 2 and 3 the IDE channels, master and slave, -d2 -d2s,
 * -d3 -d3s):
 *   +0-7          the task file (CS0): +0 data, 16 bits (D26); +7 status /
 *                 command                                                IRQ1
 *   +8-15         the control block (CS1): +14 alternate status
 *   a port with nothing on it reads 00 in every register (the card's pull-downs)
 * Every card answers at +FF0 its card ID (byte; 1 I/O, 2 disk, 3 video), +FF1
 * its revision (byte), +FF4 its interrupt status (word, a bit per device,
 * 1 = asking):
 *   I/O card:  FFFF_FFF0 / FFFF_FFF1 / FFFF_FFF4: bit 0 timer, 1 UART 0,
 *              2 UART 1, 3 UART 2, 4 UART 3, 6 USB 0, 7 USB 1, 8 network
 *   disk card: FFFF_EFF0 / FFFF_EFF1 / FFFF_EFF4: bit p = port p's INTRQ
 * The video card (D103, D104): below. It has no memory on the bus.
 *
 * As on the cards: the byte registers (UARTs, ports, CF) answer byte
 * accesses only (the byte is steered onto one lane), the timer word
 * accesses only; anything else -- or any other device-window address --
 * gets no -ACK, which the MMU / data card turns into BUSERROR. io_read /
 * io_write return 1 in that case.
 *
 * Devices respond instantly (no baud-rate or disk delays): software must not
 * depend on timing, only on the status bits, exactly as on the hardware.
 */
#ifndef P32IO_H
#define P32IO_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <netdb.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#define IO_BASE     0xFFFF8000u          /* top 32 KiB */
#define IO_UART0    0xFFFFFF00u
#define IO_UART1    0xFFFFFF08u
#define IO_UART2    0xFFFFFF30u          /* UARTs 2 and 3 (D50) */
#define IO_UART3    0xFFFFFF38u
#define TIM_COUNT   0xFFFFFF10u
#define TIM_RELOAD  0xFFFFFF14u
#define TIM_CTRL    0xFFFFFF18u          /* bit 0 enable, bit 1 pending */
#define IO_PORTIN   0xFFFFFF20u
#define IO_PORTOUT  0xFFFFFF21u
#define IO_DISK     0xFFFFEF00u          /* the disk card (D50, D52): 4 ports x 16 bytes */
#define IO_RTC      0xFFFFFF40u
#define IO_USB      0xFFFFFF50u          /* USB 0 (D43) */
#define IO_NET      0xFFFFFF52u          /* ... its network coprocessor (D45) */
#define IO_USB1     0xFFFFFF54u          /* USB 1: the second CH376S (D50) */

/* card windows (D52): 4 KiB each, the registers in the top 256 bytes */
#define WIN(n)      (0xFFFF8000u + (n) * 0x1000u)
#define WIN_IO      7                    /* the default jumpers */
#define WIN_DISK    6
#define CARD_ID     0xFF0u               /* offsets in a window: card ID (byte) */
#define CARD_REV    0xFF1u               /*   revision (byte) */
#define CARD_ISR    0xFF4u               /*   interrupt status (word) */
enum { ID_IO = 1, ID_DISK = 2, ID_VIDEO = 3, ID_DEBUG = 4 };

/* IRQ lines (D50) */
#define IRQ_TIMER   0
#define IRQ_DISK    1
#define IRQ_UART0   2
#define IRQ_UART1   3
#define IRQ_PANEL   4
#define IRQ_USB     6
#define IRQ_NET     7

/* ---------------------------------------------------------------- timer */
static uint32_t t_count, t_reload;
static int t_enable, t_pending;
static unsigned long long idle_ticks;   /* ticks skipped by WAIT */
static unsigned long long rtc_us;       /* emulated microseconds (timer ticks, idle ones too): the clock's time base */

/* The I/O card's timer exactly (p32uemu -C): io_hwedge() is the CLK rising
   edge at the end of every microcycle. A 1 MHz tick every third cycle (every
   fourth with -F, the CPU card's 4 MHz clock: D74) while enabled (the enable as it was before the edge); a TCOUNT write in the
   cycle wins; at 0 while enabled the edge reloads and sets pending -- one
   cycle after the count reached 0, so TCOUNT reads 0 for that cycle. */
static void dma_step(void);             /* the disk card's DMA engine (D75), below: once a tick */
static int t_hw, t_hw_en, t_hw_ps, t_wrcnt;
static int t_hw_div = 3;                /* -C: the prescaler; 4 with -F, the 4 MHz clock jumper (D74) */
static int t_rel_pend;                  /* -C: a TRELOAD write, taken at the cycle's end (D72) */
static uint32_t t_rel_new;
static inline void io_hwedge(void)
{
    int expire = t_hw_en && t_count == 0;
    int tick = t_hw_en && t_hw_ps == t_hw_div - 1;
    if (!t_wrcnt) {
        if (expire)
            t_count = t_reload;
        else if (tick)
            t_count--;
    }
    if (expire)
        t_pending = 1;
    dma_step();
    t_hw_ps = t_hw_en ? (t_hw_ps + 1) % t_hw_div : 0;
    t_hw_en = t_enable;
    t_wrcnt = 0;
    if (t_rel_pend) {                   /* (the reload latch clocks at this edge: a reload now used the old) */
        t_reload = t_rel_new;
        t_rel_pend = 0;
    }
}

/* -C: the I/O card's byte devices answer with one wait state (D72): a byte
   access to a UART, USB, the clock, the mailbox, the ports or the ID. */
static inline int io_byte_device(uint32_t pa, int size)
{
    uint32_t o = pa - 0xFFFFFF00u;
    if (size != 1 || pa < 0xFFFFFF00u)
        return 0;
    return o < 0x10 || (o >= 0x30 && o < 0x40) || o == 0x20 || o == 0x21 || o == 0x40 || o == 0x41 ||
           (o >= 0x50 && o < 0x56) || o == 0xF0 || o == 0xF1;
}

/* One tick per instruction (D11): count down; at 0 assert IRQ0 and reload. */
static unsigned net_ticks;              /* (the network model looks at the Mac's sockets every 4096) */
static int net_on_flag;
static void net_tick(void);
static void io_tick(void)
{
    rtc_us++;
    dma_step();
    if (net_on_flag && ++net_ticks >= 4096) { net_ticks = 0; net_tick(); }
    if (t_enable && --t_count == 0) {
        t_pending = 1;
        t_count = t_reload;
    }
}

/* ---------------------------------------------------------------- UARTs */
/* TL16C550 subset: RBR/THR, IER, IIR/FCR, LCR, MCR, LSR, MSR, SCR, DLL/DLM.
   Transmission is instant, so THR is always empty (LSR THRE and TEMT = 1). */
#define LSR_DR   0x01
#define LSR_THRE 0x20
#define LSR_TEMT 0x40
#define IER_RDA  0x01                   /* receive data available */
#define IER_THRE 0x02                   /* transmit holding register empty */

struct uart {
    FILE *in, *out;
    int peeked;                         /* -1 none, else a byte read ahead */
    uint8_t ier, lcr, mcr, scr, fcr, dll, dlm;
    int thre_int;                       /* THRE interrupt pending (16550 rules) */
    int pace;                           /* -w: input a line at a time, after a prompt */
    char tail[16];                      /* the last bytes sent (for -w) */
    int tlen;
    char line[512];                     /* the input line being delivered (for -w) */
    int lpos, llen;
    int fd;                             /* interactive: a raw, non-blocking terminal or socket (-1: none) */
    int lfd;                            /* -T: the listening socket (-1: none) */
    int poll;                           /* (interactive input is looked at every 256th check) */
};
/* -w: a test aid for interrupt-driven programs (P32W Unix): each input line
   is held back until the output ends with one of these prompts at the start
   of a line, so scripted
   sessions read like typed ones. -P adds a prompt (the first replaces these). */
static const char *prompts[12] = { "# ", "$ ", "> ", "login: ", "Password: ", "Ok\r\n", "*? ", ">>> ", "... " };
static int nprompts = 9, prompts_set;   /* (BASIC's Ok, and its INPUT's "? "; Python's >>> and ...;
                                           * : anywhere in a line) */
#define NUART 4                         /* the I/O card's four (D50) */
static struct uart uart[NUART] = {
    { NULL, NULL, -1, 0, 0, 0, 0, 0, 0x0C, 0, 0, 0, {0}, 0, {0}, 0, 0, -1, -1, 0 },
    { NULL, NULL, -1, 0, 0, 0, 0, 0, 0x0C, 0, 0, 0, {0}, 0, {0}, 0, 0, -1, -1, 0 },
    { NULL, NULL, -1, 0, 0, 0, 0, 0, 0x0C, 0, 0, 0, {0}, 0, {0}, 0, 0, -1, -1, 0 },
    { NULL, NULL, -1, 0, 0, 0, 0, 0, 0x0C, 0, 0, 0, {0}, 0, {0}, 0, 0, -1, -1, 0 },
};
static int io_trace;                    /* set by the emulator's -t */

/* -dbg PORT (D97, p32dbg.h): the debug server. UART 0 is then the server's:
   what P32W sends waits for the front end's console command, and its key
   command queues what P32W receives. The server's connection wakes WAIT. */
static int dbg_console, dbg_port = -1;
static int dbg_fd = -1, dbg_wake;       /* the front end's connection; it has sent something */
static uint8_t dbg_keys[65536];
static unsigned dbg_khead, dbg_ktail;
static char *dbg_con;
static size_t dbg_con_len, dbg_con_cap;
static unsigned long long dbg_con_lost;
static void dbg_con_put(int c)
{
    if (dbg_con_len == dbg_con_cap) {
        if (dbg_con_cap >= 1u << 20) { dbg_con_lost++; return; }    /* (1 MB not fetched: the rest is dropped) */
        dbg_con_cap = dbg_con_cap ? dbg_con_cap * 2 : 4096;
        if (!(dbg_con = realloc(dbg_con, dbg_con_cap))) exit(3);
    }
    dbg_con[dbg_con_len++] = (char)c;
}

static int prompted(struct uart *u)
{
    int k;
    for (k = 0; k < nprompts; k++) {
        const char *p = prompts[k];
        int any = *p == '*', n;
        if (any) p++;
        n = (int)strlen(p);                     /* a prompt begins a line (unless marked *) */
        if (n < u->tlen && !memcmp(u->tail + u->tlen - n, p, (size_t)n) &&
            (any || u->tail[u->tlen - n - 1] == '\n'))
            return 1;
    }
    return 0;
}

static struct termios tty_saved;
static unsigned long long disk_rd, disk_wr, disk_cmds, con_bytes;   /* counted for -G: sectors, commands, console output */
static int tty_raw;
static void io_restore(void)
{
    if (tty_raw) { tcsetattr(0, TCSANOW, &tty_saved); tty_raw = 0; }
}

/* interactive input: a byte if one has come, without waiting */
static int uart_poll(struct uart *u)
{
    unsigned char c;
    ssize_t n;
    if (++u->poll < 256)
        return 0;
    u->poll = 0;
    if (u->fd < 0) {                    /* -T: a connection yet? */
        int c2 = accept(u->lfd, NULL, NULL);
        if (c2 < 0)
            return 0;
        fcntl(c2, F_SETFL, O_NONBLOCK);
        u->fd = c2;
        u->out = fdopen(dup(c2), "w");
        if (u->out) setvbuf(u->out, NULL, _IONBF, 0);
        fprintf(stderr, "[UART %d: connected]\n", (int)(u - uart));
    }
    n = read(u->fd, &c, 1);
    if (n == 1) {
        if (c == 0x1D && u == &uart[0]) {           /* ^]: leave the emulator */
            io_restore();
            fprintf(stderr, "\n[p32emu: ^] -- stopped]\n");
            exit(0);
        }
        u->peeked = c;
        return 1;
    }
    if (n == 0 && u->lfd >= 0) {        /* the other end hung up: wait for the next */
        close(u->fd);
        if (u->out) fclose(u->out);
        u->out = NULL;
        u->fd = -1;
        fprintf(stderr, "[UART %d: disconnected]\n", (int)(u - uart));
    }
    return 0;
}

static int uart_ready(struct uart *u)   /* receive data ready? (reads ahead) */
{
    if (u->peeked >= 0)
        return 1;
    if (dbg_console && u == &uart[0]) {         /* -dbg: the front end's keys */
        if (dbg_khead == dbg_ktail)
            return 0;
        u->peeked = dbg_keys[dbg_ktail++ & 0xFFFF];
        return 1;
    }
    if (u->fd >= 0 || u->lfd >= 0)
        return uart_poll(u);
    if (!u->in)
        return 0;
    if (u->pace) {
        if (u->lpos >= u->llen) {
            if (!prompted(u) || !fgets(u->line, sizeof u->line, u->in))
                return 0;
            u->llen = (int)strlen(u->line);
            u->lpos = 0;
            u->tlen = 0;                /* (this prompt is answered) */
        }
        u->peeked = (unsigned char)u->line[u->lpos++];
        return 1;
    }
    int c = getc(u->in);
    if (c == EOF)
        return 0;
    u->peeked = c;
    return 1;
}

static int uart_irq(struct uart *u)
{
    return ((u->ier & IER_RDA) && uart_ready(u)) || ((u->ier & IER_THRE) && u->thre_int);
}

static uint8_t uart_read(struct uart *u, unsigned reg)
{
    int dlab = u->lcr & 0x80;
    switch (reg) {
    case 0:
        if (dlab)
            return u->dll;
        if (uart_ready(u)) {
            int c = u->peeked;
            u->peeked = -1;
            return (uint8_t)c;
        }
        return 0;
    case 1: return dlab ? u->dlm : u->ier;
    case 2: {                           /* IIR: highest-priority source, bit 0 = 0 when pending */
        uint8_t fifo = (u->fcr & 1) ? 0xC0 : 0;
        if ((u->ier & IER_RDA) && uart_ready(u))
            return fifo | 0x04;
        if ((u->ier & IER_THRE) && u->thre_int) {
            u->thre_int = 0;            /* reading IIR clears a THRE interrupt */
            return fifo | 0x02;
        }
        return fifo | 0x01;
    }
    case 3: return u->lcr;
    case 4: return u->mcr;
    case 5: return (uint8_t)((uart_ready(u) ? LSR_DR : 0) | LSR_THRE | LSR_TEMT);
    case 6: return 0xB0;                /* MSR: DCD, DSR, CTS asserted */
    default: return u->scr;
    }
}

static void uart_write(struct uart *u, unsigned reg, uint8_t v)
{
    int dlab = u->lcr & 0x80;
    switch (reg) {
    case 0:
        if (dlab) { u->dll = v; return; }
        if (u == &uart[0]) con_bytes++;
        if (dbg_console && u == &uart[0]) dbg_con_put(v);
        if (u->out) {
            putc(v, u->out);
            if (io_trace)
                fflush(u->out);
        }
        if (u->tlen == (int)sizeof u->tail) {
            memmove(u->tail, u->tail + 1, sizeof u->tail - 1);
            u->tlen--;
        }
        u->tail[u->tlen++] = (char)v;
        u->thre_int = 1;                /* the byte is gone at once: THR empty again */
        return;
    case 1:
        if (dlab) { u->dlm = v; return; }
        if ((v & IER_THRE) && !(u->ier & IER_THRE))
            u->thre_int = 1;            /* enabling THRE with THR empty interrupts at once */
        u->ier = v & 0x0F;
        return;
    case 2: u->fcr = v; return;
    case 3: u->lcr = v; return;
    case 4: u->mcr = v & 0x1F; return;
    case 7: u->scr = v; return;
    default: return;                    /* LSR, MSR: read-only */
    }
}

/* ---------------------------------------------------------------- ports */
static uint8_t port_in, port_out;
static void (*port_hook)(uint8_t);      /* a write to the output port (p32uemu -G, D85) */

/* ---------------------------------------------------------------- CompactFlash */
/* True-IDE task file, 8-bit transfers (SET FEATURES 01h; the driver must
   enable it, as on a real card). Commands: READ SECTOR(S) 20h/21h, WRITE
   SECTOR(S) 30h/31h, IDENTIFY DEVICE ECh, SET FEATURES EFh, and the no-op
   power/cache commands; anything else aborts (ERR, error = ABRT). */
#define CF_ERR  0x01
#define CF_DRQ  0x08
#define CF_DSC  0x10
#define CF_DRDY 0x40
#define CF_BSY  0x80

struct ata {
    FILE *img;                          /* NULL: no card in the adapter */
    int fixed;                          /* an IDE drive (not a CF card): what IDENTIFY says */
    uint32_t sectors;
    uint8_t feat, err, count, lba0, lba1, lba2, head, status;
    int eightbit;
    uint8_t buf[512];
    unsigned pos;                       /* bytes transferred in the buffer */
    int dir;                            /* 0 idle, 1 reading (to host), 2 writing */
    uint32_t lba;
    unsigned left;                      /* sectors left in the command */
    int intrq;
};
/* the disk card (D50, D52): four ports, a master and a slave on each
   (0: the boot CF card, -d; 1: the second CF socket, -d1; 2, 3: the IDE
   channels, -d2 / -d2s, -d3 / -d3s). Both drives on a port take the
   task-file writes, as on an IDE cable; the one the head register's DEV bit
   selects answers the reads and takes the command. The model below works
   on `cf`, the drive being accessed: ata_on() points it */
static struct ata atas[4][2], *cfp = &atas[0][0];
static int ata_sel[4];
#define cf (*cfp)
static void ata_on(unsigned port, int unit) { cfp = &atas[port][unit]; }
static int ata_intrq(unsigned port) { return atas[port][0].intrq || atas[port][1].intrq; }

static uint32_t cf_lba(void)
{
    return (uint32_t)cf.lba0 | (uint32_t)cf.lba1 << 8 | (uint32_t)cf.lba2 << 16 |
           (uint32_t)(cf.head & 0x0F) << 24;
}

static void cf_abort(uint8_t e)
{
    cf.err = e;
    cf.status = CF_DRDY | CF_DSC | CF_ERR;
    cf.dir = 0;
    cf.intrq = 1;
}

static int cf_load(void)                /* next sector into the buffer; 0 ok */
{
    if (cf.lba >= cf.sectors) { cf_abort(0x10); return 1; }   /* IDNF */
    if (fseek(cf.img, (long)cf.lba * 512, SEEK_SET) || fread(cf.buf, 1, 512, cf.img) != 512)
        memset(cf.buf, 0, 512);         /* past the end of a short image: zeros */
    disk_rd++;
    cf.pos = 0;
    cf.status = CF_DRDY | CF_DSC | CF_DRQ;
    cf.intrq = 1;
    return 0;
}

static void cf_put16(unsigned w, uint16_t v) { cf.buf[2 * w] = (uint8_t)v; cf.buf[2 * w + 1] = (uint8_t)(v >> 8); }
static void cf_putstr(unsigned w, unsigned n, const char *s)  /* ATA strings: byte-swapped pairs */
{
    for (unsigned i = 0; i < n; i++) {
        char a = *s ? *s++ : ' ', b = *s ? *s++ : ' ';
        cf.buf[2 * (w + i)] = (uint8_t)b;
        cf.buf[2 * (w + i) + 1] = (uint8_t)a;
    }
}

static void cf_command(uint8_t c)
{
    cf.err = 0;
    cf.intrq = 0;
    if (!cf.img) { cf_abort(0x04); return; }
    switch (c) {
    case 0x20: case 0x21:               /* READ SECTOR(S) */
        if (!(cf.head & 0x40)) { cf_abort(0x04); return; }    /* LBA mode only */
        cf.lba = cf_lba(); cf.left = cf.count ? cf.count : 256; cf.dir = 1;
        disk_cmds++;
        cf_load();
        return;
    case 0x30: case 0x31:               /* WRITE SECTOR(S) */
        if (!(cf.head & 0x40)) { cf_abort(0x04); return; }
        cf.lba = cf_lba(); cf.left = cf.count ? cf.count : 256; cf.dir = 2;
        disk_cmds++;
        if (cf.lba >= cf.sectors) { cf_abort(0x10); return; }
        cf.pos = 0; cf.status = CF_DRDY | CF_DSC | CF_DRQ;    /* no interrupt for the first block */
        return;
    case 0xEC:                          /* IDENTIFY DEVICE */
        memset(cf.buf, 0, 512);
        cf_put16(0, cf.fixed ? 0x0040 : 0x848A);    /* a fixed disk, or the CompactFlash signature */
        cf_put16(1, (uint16_t)(cf.sectors / (16 * 63) ? cf.sectors / (16 * 63) : 1));
        cf_put16(3, 16); cf_put16(6, 63);
        cf_putstr(10, 10, "P32EMU0001");
        cf_putstr(23, 4, "1.0");
        cf_putstr(27, 20, cf.fixed ? "P32W EMULATED IDE DISK" : "P32W EMULATED CF CARD");
        cf_put16(47, 1);
        cf_put16(49, 0x0200);           /* LBA supported */
        cf_put16(60, (uint16_t)cf.sectors); cf_put16(61, (uint16_t)(cf.sectors >> 16));
        cf.dir = 1; cf.left = 1; cf.pos = 0;
        cf.status = CF_DRDY | CF_DSC | CF_DRQ; cf.intrq = 1;
        return;
    case 0xEF:                          /* SET FEATURES */
        if (cf.feat == 0x01) cf.eightbit = 1;
        else if (cf.feat == 0x81) cf.eightbit = 0;
        cf.status = CF_DRDY | CF_DSC; cf.intrq = 1;
        return;
    case 0xE0: case 0xE1: case 0xE5: case 0xE7: case 0x91:    /* power, flush, params: no-ops */
        cf.status = CF_DRDY | CF_DSC; cf.intrq = 1;
        return;
    default:
        cf_abort(0x04);                 /* ABRT */
    }
}

/* the data register as a halfword (D26: the I/O card's 16-bit CF path): two
   bytes, the first in bits 0-7; in 8-bit mode only DD0-7 carry data */
static int cf_sector_done(void);
static uint32_t cf_read16(void)
{
    uint32_t v;
    if (!cf.img || cf.dir != 1 || !(cf.status & CF_DRQ))
        return 0;
    if (cf.eightbit)
        v = 0xFF00u | cf.buf[cf.pos++];
    else {
        v = (uint32_t)cf.buf[cf.pos] | (uint32_t)cf.buf[cf.pos + 1] << 8;
        cf.pos += 2;
    }
    if (cf.pos >= 512) {
        cf.lba++;
        if (--cf.left == 0) { cf.dir = 0; cf.status = CF_DRDY | CF_DSC; }
        else cf_load();
    }
    return v;
}

static void cf_write16(uint32_t v)
{
    if (!cf.img || cf.dir != 2 || !(cf.status & CF_DRQ))
        return;
    cf.buf[cf.pos++] = (uint8_t)v;
    if (!cf.eightbit)
        cf.buf[cf.pos++] = (uint8_t)(v >> 8);
    cf_sector_done();
}

static uint8_t cf_read(unsigned reg)
{
    if (!cf.img)
        return reg == 7 ? 0x00 : 0xFF;  /* empty adapter: not ready, never busy */
    switch (reg) {
    case 0: {
        if (cf.dir != 1 || !(cf.status & CF_DRQ))
            return 0;
        uint8_t v = cf.buf[cf.pos++];
        if (!cf.eightbit)
            cf.pos++;                   /* 16-bit mode: the high byte is lost on an 8-bit bus */
        if (cf.pos >= 512) {
            cf.lba++;
            if (--cf.left == 0) { cf.dir = 0; cf.status = CF_DRDY | CF_DSC; }
            else cf_load();
        }
        return v;
    }
    case 1: return cf.err;
    case 2: return cf.count;
    case 3: return cf.lba0;
    case 4: return cf.lba1;
    case 5: return cf.lba2;
    case 6: return cf.head;
    default:
        cf.intrq = 0;                   /* reading Status acknowledges INTRQ */
        return cf.status;
    }
}

static void cf_write(unsigned reg, uint8_t v)
{
    switch (reg) {
    case 0:
        if (!cf.img || cf.dir != 2 || !(cf.status & CF_DRQ))
            return;
        cf.buf[cf.pos++] = v;
        if (!cf.eightbit)
            cf.buf[cf.pos++] = 0;       /* 16-bit mode: a byte write leaves the high byte 0 */
        cf_sector_done();
        return;
    case 1: cf.feat = v; return;
    case 2: cf.count = v; return;
    case 3: cf.lba0 = v; return;
    case 4: cf.lba1 = v; return;
    case 5: cf.lba2 = v; return;
    case 6: cf.head = v; return;
    default: cf_command(v); return;
    }
}

static int cf_sector_done(void)        /* a written sector full: to the image */
{
    if (cf.pos < 512)
        return 0;
    if (fseek(cf.img, (long)cf.lba * 512, SEEK_SET) == 0)
        fwrite(cf.buf, 1, 512, cf.img), fflush(cf.img);
    disk_wr++;
    cf.lba++; cf.pos = 0; cf.intrq = 1;
    if (--cf.left == 0) { cf.dir = 0; cf.status = CF_DRDY | CF_DSC; }
    else if (cf.lba >= cf.sectors) cf_abort(0x10);
    return 1;
}

/* ---------------------------------------------------------------- the disk card's DMA engine (D75)
   FF40 DMAADR: the physical address of the next word (word aligned).
   FF44 DMACNT: the words left (16 bits).
   FF48 DMACTL: write bit 0 RUN (1 starts; 0 stops after the word in hand),
   1 DIR (0 the drive to memory, 1 memory to the drive), 3:2 the port, 4 IE,
   5 MODE (multiword DMA: reserved, 0); a write clears DONE and ERR. Read:
   bit 0 BUSY, 1 DIR, 3:2 the port, 4 IE, 5 MODE, 6 DONE, 7 ERR.
   A word is two reads (or writes) of the port's data register, the first
   halfword the low one, and one 32-bit bus cycle; a cycle no card answers
   stops the engine with ERR, that word not counted. DONE or ERR with IE
   is the card's interrupt source 4 (status bit 4, vector window x 16 + 4).
   The engine moves a word every dma_ticks ticks (one a microcycle in
   p32uemu: about the card's 11 clocks; one an instruction in p32emu); a
   WAIT finishes the transfer at once. Memory is the emulator's: dma_map()
   gives a host pointer, or NULL where no card answers. */
#define DMA_ADR 0x40u
#define DMA_CNT 0x44u
#define DMA_CTL 0x48u
static uint32_t dma_adr;                /* the byte address of the next word */
static unsigned dma_cnt, dma_ctl, dma_done, dma_err, dma_wait;
static unsigned dma_ticks = 4;          /* (p32uemu: 11, the card's clocks a word) */
static uint8_t *(*dma_map)(uint32_t pa, int write);
#define DMA_RUN  (dma_ctl & 1u)
#define DMA_DIR  (dma_ctl >> 1 & 1u)
#define DMA_PORT (dma_ctl >> 2 & 3u)
#define DMA_IE   (dma_ctl >> 4 & 1u)
static int dma_busy(void) { return DMA_RUN && dma_cnt; }
static int dma_irq(void) { return DMA_IE && (dma_done || dma_err); }
static uint32_t dma_status(void)
{
    return (dma_busy() ? 1u : 0) | (dma_ctl & 0x3Eu) | (dma_done ? 0x40u : 0) | (dma_err ? 0x80u : 0);
}
static void dma_word(void)
{
    unsigned port = DMA_PORT;
    int present = atas[port][0].img || atas[port][1].img;
    uint8_t *p = dma_map ? dma_map(dma_adr, DMA_DIR == 0) : NULL;
    if (!p) {                           /* no -ACK: the word is not counted */
        dma_err = 1;
        dma_ctl &= ~1u;
        return;
    }
    ata_on(port, ata_sel[port]);
    if (DMA_DIR == 0) {                 /* the drive's two halfwords, then the bus write */
        uint32_t lo = present ? (cf.img ? cf_read16() : 0xFFFF) : 0;
        uint32_t hi = present ? (cf.img ? cf_read16() : 0xFFFF) : 0;
        uint32_t w = lo | hi << 16;
        p[0] = (uint8_t)w; p[1] = (uint8_t)(w >> 8); p[2] = (uint8_t)(w >> 16); p[3] = (uint8_t)(w >> 24);
    } else {                            /* the bus read, then the two halfwords */
        uint32_t w = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        if (present) {
            cf_write16(w & 0xFFFF);
            cf_write16(w >> 16);
        }
    }
    dma_adr += 4;
    if (--dma_cnt == 0) {
        dma_done = 1;
        dma_ctl &= ~1u;
    }
}
static void dma_step(void)
{
    if (dma_busy() && ++dma_wait >= dma_ticks) {
        dma_wait = 0;
        dma_word();
    }
}
static int dma_reg_read(unsigned o, int size, uint32_t *v)
{
    if (size != 4)
        return 1;
    *v = o == DMA_ADR ? dma_adr : o == DMA_CNT ? dma_cnt : dma_status();
    return 0;
}
static int dma_reg_write(unsigned o, int size, uint32_t v)
{
    if (size != 4)
        return 1;
    if (o == DMA_ADR) dma_adr = v & ~3u;
    else if (o == DMA_CNT) dma_cnt = v & 0xFFFF;
    else {
        dma_ctl = v & 0x3Fu;
        dma_done = dma_err = 0;
        dma_wait = 0;
        if (DMA_RUN && !dma_cnt) { dma_done = 1; dma_ctl &= ~1u; }
    }
    return 0;
}

/* ---------------------------------------------------------------- USB: the CH376S (D43)
   The I/O card's USB host controllers, in their sector mode (WCH's
   CH376 datasheet; docs/usb-design.md), with a drive whose contents are an
   image file (-U). FF51 written is a command; read, the status: bit 7 is
   INT#, 0 while an interrupt is pending; bit 4 busy, always 0 here (the
   model answers at once, as the other devices do: software must wait for the
   interrupt, never on time). FF50 is the command's data, in and out.
   A command that ends with an interrupt sets the interrupt status; GET_STATUS
   reads it and clears INT#. Sectors move in 64-byte chunks: DISK_READ, then
   for each chunk USB_INT_DISK_READ, RD_USB_DATA0 (a length byte, then the
   bytes), DISK_RD_GO; at the end USB_INT_SUCCESS. Writing is the same with
   DISK_WRITE, USB_INT_DISK_WRITE, WR_HOST_DATA and DISK_WR_GO.
   -Uout N pulls the drive out after N sectors have moved (tests: a drive
   removed mid-transfer). */
#define CH_GET_IC_VER   0x01
#define CH_RESET_ALL    0x05
#define CH_CHECK_EXIST  0x06
#define CH_SET_USB_MODE 0x15
#define CH_GET_STATUS   0x22
#define CH_RD_USB_DATA0 0x27
#define CH_WR_HOST_DATA 0x2C
#define CH_DISK_CONNECT 0x30
#define CH_DISK_MOUNT   0x31
#define CH_DISK_CAPACITY 0x3E
#define CH_DISK_INIT    0x51
#define CH_DISK_READ    0x54
#define CH_DISK_RD_GO   0x55
#define CH_DISK_WRITE   0x56
#define CH_DISK_WR_GO   0x57
#define CH_DISK_R_SENSE 0x5A
#define CH_RET_SUCCESS  0x51
#define CH_RET_ABORT    0x5F
#define CH_INT_SUCCESS  0x14
#define CH_INT_CONNECT  0x15
#define CH_INT_DISCONNECT 0x16
#define CH_INT_DISK_READ  0x1D
#define CH_INT_DISK_WRITE 0x1E
#define CH_INT_DISK_ERR   0x1F

struct ch376 {
    FILE *img;
    uint32_t sectors;                   /* the drive's size (512-byte sectors) */
    int attached;                       /* a drive is plugged in */
    long out_after;                     /* -Uout: sectors left before it is pulled out (-1: never) */
    int host;                           /* SET_USB_MODE 6 or 7 done: host mode */
    int intr;                           /* INT# low */
    uint8_t istat;                      /* the interrupt status */
    uint8_t cmd;                        /* the command whose data FF50 moves */
    int nin;                            /* data bytes written for it so far */
    uint8_t in[8];
    uint8_t out[80];                    /* bytes for FF50 to read, and how many */
    int nout, pout;
    uint8_t pend[8];                    /* a command's result, waiting for RD_USB_DATA0 */
    int npend;
    uint32_t lba;                       /* a transfer: the next sector, sectors left */
    int left, chunk;                    /* (chunk: 64-byte piece of the sector, 0-7) */
    uint8_t sec[512];
};
/* the I/O card has two (D50): USB 0 at FF50, USB 1 at FF54. The
   model below works on `usb`, the one being accessed: usb_on() points it */
static struct ch376 usbs[2], *usbp = &usbs[0];
#define usb (*usbp)
static void usb_on(int n) { usbp = &usbs[n]; }

static void usb_int(uint8_t st) { usb.istat = st; usb.intr = 1; }

static void usb_reply(const uint8_t *b, int n)  /* what FF50 reads next */
{
    memcpy(usb.out, b, (size_t)n);
    usb.nout = n;
    usb.pout = 0;
}

static void usb_pull(void)              /* the drive taken out */
{
    usb.attached = 0;
    usb.left = 0;
    if (usb.host) usb_int(CH_INT_DISCONNECT);
}

static int usb_moved(void)              /* a sector moved: -Uout's count; 1 if the drive is gone */
{
    if (usb.out_after > 0 && --usb.out_after == 0) { usb_pull(); return 1; }
    return 0;
}

static int usb_load(void)               /* the transfer's next sector into sec; 0 ok */
{
    if (!usb.attached || usb.lba >= usb.sectors) return -1;
    fseek(usb.img, (long)usb.lba * 512, SEEK_SET);
    if (fread(usb.sec, 1, 512, usb.img) != 512) memset(usb.sec, 0, 512);
    return 0;
}

static void usb_command(uint8_t c)
{
    static const uint8_t ok = CH_RET_SUCCESS;
    usb.cmd = c;
    usb.nin = 0;
    usb.nout = usb.pout = 0;
    switch (c) {
    case CH_GET_IC_VER: { uint8_t v = 0x43; usb_reply(&v, 1); break; }
    case CH_RESET_ALL:
        usb.host = usb.intr = 0;
        usb.left = 0;
        break;
    case CH_GET_STATUS:
        usb_reply(&usb.istat, 1);
        usb.intr = 0;
        break;
    case CH_DISK_CONNECT:
        usb_int(usb.attached ? CH_INT_SUCCESS : CH_INT_DISCONNECT);
        break;
    case CH_DISK_INIT: case CH_DISK_MOUNT:
        usb_int(usb.attached && usb.host ? CH_INT_SUCCESS : CH_INT_DISK_ERR);
        break;
    case CH_DISK_CAPACITY:
        if (usb.attached && usb.host) {
            uint8_t b[5] = { 4, (uint8_t)usb.sectors, (uint8_t)(usb.sectors >> 8), (uint8_t)(usb.sectors >> 16), (uint8_t)(usb.sectors >> 24) };
            memcpy(usb.pend, b, 5);     /* (RD_USB_DATA0 reads it: a length, then the count) */
            usb.npend = 5;
            usb_int(CH_INT_SUCCESS);
        } else usb_int(CH_INT_DISK_ERR);
        break;
    case CH_DISK_R_SENSE:
        usb_int(usb.attached ? CH_INT_SUCCESS : CH_INT_DISK_ERR);
        break;
    case CH_RD_USB_DATA0:               /* a chunk being read, or a command's result */
        if (usb.left > 0 && usb.istat == CH_INT_DISK_READ) {
            uint8_t b[65];
            b[0] = 64;
            memcpy(b + 1, usb.sec + 64 * usb.chunk, 64);
            usb_reply(b, 65);
        } else if (usb.npend) {
            usb_reply(usb.pend, usb.npend);
            usb.npend = 0;
        } else {
            uint8_t z = 0;              /* (nothing to read: a length of 0) */
            usb_reply(&z, 1);
        }
        break;
    case CH_DISK_RD_GO:
        if (usb.left <= 0 || !usb.attached) { usb_int(usb.attached ? CH_INT_DISK_ERR : CH_INT_DISCONNECT); break; }
        if (++usb.chunk == 8) {         /* the sector read out */
            usb.chunk = 0;
            usb.lba++;
            if (usb_moved()) break;
            if (--usb.left == 0) { usb_int(CH_INT_SUCCESS); break; }
            if (usb_load() < 0) { usb.left = 0; usb_int(CH_INT_DISK_ERR); break; }
        }
        usb_int(CH_INT_DISK_READ);
        break;
    case CH_DISK_WR_GO:
        if (usb.left <= 0 || !usb.attached) { usb_int(usb.attached ? CH_INT_DISK_ERR : CH_INT_DISCONNECT); break; }
        if (++usb.chunk == 8) {         /* a sector filled: to the image */
            usb.chunk = 0;
            fseek(usb.img, (long)usb.lba * 512, SEEK_SET);
            fwrite(usb.sec, 1, 512, usb.img);
            fflush(usb.img);
            usb.lba++;
            if (usb_moved()) break;
            if (--usb.left == 0) { usb_int(CH_INT_SUCCESS); break; }
        }
        usb_int(CH_INT_DISK_WRITE);
        break;
    }
    /* the commands that take data first (CHECK_EXIST, SET_USB_MODE, DISK_READ,
       DISK_WRITE, WR_HOST_DATA) act when it has come: usb_data_write */
    (void)ok;
}

static void usb_data_write(uint8_t v)
{
    static const uint8_t ok = CH_RET_SUCCESS, abort_ = CH_RET_ABORT;
    switch (usb.cmd) {
    case CH_CHECK_EXIST: { uint8_t r = (uint8_t)~v; usb_reply(&r, 1); break; }
    case CH_SET_USB_MODE:
        usb.host = v == 6 || v == 7;
        usb_reply(usb.host || v <= 5 ? &ok : &abort_, 1);
        if (v == 6 && usb.attached) usb_int(CH_INT_CONNECT);   /* a drive already plugged in */
        break;
    case CH_DISK_READ: case CH_DISK_WRITE:
        if (usb.nin < 5) usb.in[usb.nin++] = v;
        if (usb.nin == 5) {             /* LBA (4 bytes, low first), count */
            usb.lba = usb.in[0] | (uint32_t)usb.in[1] << 8 | (uint32_t)usb.in[2] << 16 | (uint32_t)usb.in[3] << 24;
            usb.left = usb.in[4];
            usb.chunk = 0;
            if (!usb.attached || !usb.host || !usb.left || usb.lba + usb.left > usb.sectors) {
                usb.left = 0;
                usb_int(usb.attached ? CH_INT_DISK_ERR : CH_INT_DISCONNECT);
            } else if (usb.cmd == CH_DISK_READ) {
                usb_load();
                usb_int(CH_INT_DISK_READ);
            } else
                usb_int(CH_INT_DISK_WRITE);
        }
        break;
    case CH_WR_HOST_DATA:               /* a length byte (64), then the chunk */
        if (usb.nin++ == 0) break;
        if (usb.left > 0 && usb.nin - 2 < 64) usb.sec[64 * usb.chunk + usb.nin - 2] = v;
        break;
    }
}

static uint8_t usb_read(unsigned reg)
{
    if (reg == 1)                       /* status: bit 7 = INT# (0: pending) */
        return usb.intr ? 0x00 : 0x80;
    return usb.pout < usb.nout ? usb.out[usb.pout++] : 0;
}

static void usb_write(unsigned reg, uint8_t v)
{
    if (reg == 1) usb_command(v);
    else usb_data_write(v);
}

/* ---------------------------------------------------------------- the network coprocessor (D45)
   The I/O card's network coprocessor (a Pico 2 W with a W5500) as P32W
   sees it: two byte registers and the request / reply protocol of
   docs/net-mailbox.md, carried out with the Mac's own sockets, so P32W in the
   emulator really reaches the network. 10.0.2.2 is the Mac (127.0.0.1);
   listening ports below 1024 move up by -Nport (8000); TLS goes through
   tools/p32tls.py (Python's ssl), a child process on a socket pair. Without
   -N the card is there but its coprocessor is not running (READY 0). */
#define NET_NSOCK  8
#define NET_MAXPAY 1024
enum { NS_FREE, NS_NEW, NS_CONNECTING, NS_CONNECTED, NS_LISTENING, NS_PEERCLOSED, NS_FAILED, NS_BOUND };
enum { NE_OK, NE_AGAIN, NE_INVAL, NE_NOSOCK, NE_REFUSED, NE_TIMEDOUT, NE_UNREACH, NE_RESET, NE_NOTCONN,
       NE_INUSE, NE_NONAME, NE_TLS, NE_NOLINK, NE_TOOBIG, NE_BADCMD };
enum { NT_TCP = 1, NT_UDP, NT_TLS, NT_DNS, NT_ICMP };
#define NET_DGRAM(t) ((t) == NT_UDP || (t) == NT_ICMP)
#define NST_RXF   0x01
#define NST_TXE   0x02
#define NST_ATTN  0x04
#define NST_IEN   0x08
#define NST_READY 0x80

struct nsock {
    int type, state, err;
    int fd;                             /* the Mac's socket (TLS: our end of the helper's socket pair), or -1 */
    pid_t pid;                          /* TLS: the helper */
    uint16_t lport, pport;              /* P32W's local port; the peer's */
    uint32_t pip;                       /* the peer's address (host order) */
    uint8_t dns[4];                     /* DNS: the answer */
    int seen;                           /* what the last look found (state, readable, writable): for EVENTS */
    int icmpid;                         /* ICMP: the echo identifier P32W sent last, or -1 */
};

static struct {
    int on, ien, attn;
    unsigned offset;                    /* -Nport: listening ports below 1024 move up by this */
    const char *ca;                     /* -Nca: trusted certificates for TLS */
    char helper[1024];                  /* tools/p32tls.py, beside the emulator's directory */
    uint8_t req[4 + NET_MAXPAY];
    int nreq, discard;                  /* (discard: payload bytes of a too-long request still to come) */
    uint8_t rep[4 + NET_MAXPAY];
    int nrep, prep;
    unsigned events;                    /* sockets changed since the last EVENTS */
    unsigned polls;
    uint8_t ifc[16];                    /* address, netmask, gateway, DNS server (IFCONFIG) */
    int static_;                        /* set by IFCONFIG, not DHCP */
    struct nsock s[NET_NSOCK];
} net = { .offset = 8000, .ifc = { 10, 0, 2, 15, 255, 255, 255, 0, 10, 0, 2, 2, 10, 0, 2, 3 } };

static uint32_t net_g32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static unsigned net_g16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static void net_p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void net_p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* P32W's view of an address and the Mac's: 10.0.2.2 is the Mac's loopback */
static uint32_t net_tohost(uint32_t a) { return a == 0x0A000202u ? 0x7F000001u : a; }
static uint32_t net_fromhost(uint32_t a) { return (a >> 24) == 127 ? 0x0A000202u : a; }

static int net_errcode(int e)           /* a Mac errno as a status code */
{
    switch (e) {
    case ECONNREFUSED: return NE_REFUSED;
    case ETIMEDOUT: return NE_TIMEDOUT;
    case EHOSTUNREACH: case ENETUNREACH: case EADDRNOTAVAIL: return NE_UNREACH;
    case ECONNRESET: case EPIPE: return NE_RESET;
    case ENOTCONN: return NE_NOTCONN;
    case EADDRINUSE: return NE_INUSE;
    case ENETDOWN: return NE_NOLINK;
    default: return NE_RESET;
    }
}

static void net_free(struct nsock *s)
{
    if (s->fd >= 0) close(s->fd);
    if (s->pid > 0) { kill(s->pid, SIGTERM); waitpid(s->pid, NULL, 0); }
    memset(s, 0, sizeof *s);
    s->fd = -1;
    s->icmpid = -1;
}

static void net_fail(struct nsock *s, int code)
{
    s->state = NS_FAILED;
    s->err = code;
}

static int net_newfd(int type)
{
    int fd = socket(AF_INET, type, 0), one = 1;
    if (fd < 0) return -1;
    fcntl(fd, F_SETFL, O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    (void)one;
    return fd;
}

/* how socket s stands now: its state brought up to date, and what a waiting
   process could do (bit 3: data or the end to read, bit 4: room to send,
   bit 5: a connection to accept) */
static int net_look(struct nsock *s)
{
    struct pollfd p;
    int bits = 0, avail = 0;
    if (s->fd < 0) return s->state;
    p.fd = s->fd;
    p.events = POLLIN | POLLOUT;
    p.revents = 0;
    if (poll(&p, 1, 0) < 0) return s->state;
    if (s->state == NS_CONNECTING && s->type == NT_TCP && (p.revents & (POLLOUT | POLLERR | POLLHUP))) {
        int e = 0;
        socklen_t n = sizeof e;
        getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &e, &n);
        if (e) net_fail(s, net_errcode(e));
        else {
            struct sockaddr_in a;
            socklen_t al = sizeof a;
            s->state = NS_CONNECTED;
            if (getsockname(s->fd, (struct sockaddr *)&a, &al) == 0) s->lport = ntohs(a.sin_port);
        }
    }
    if (s->state == NS_CONNECTING && s->type == NT_TLS && (p.revents & (POLLIN | POLLHUP))) {
        char c = 0;                     /* the helper's first byte: K, or why not */
        ssize_t r = read(s->fd, &c, 1);
        if (r == 1 && c == 'K') s->state = NS_CONNECTED;
        else net_fail(s, c == 'R' ? NE_REFUSED : c == 'T' ? NE_TIMEDOUT : c == 'U' ? NE_UNREACH : NE_TLS);
        p.revents = 0;                  /* (look again next time) */
    }
    if ((s->state == NS_CONNECTED || s->state == NS_PEERCLOSED || s->state == NS_BOUND) && (p.revents & (POLLIN | POLLHUP | POLLERR))) {
        char c;
        ioctl(s->fd, FIONREAD, &avail);
        if (!NET_DGRAM(s->type) && avail == 0) {         /* readable with nothing in it: the end, or an error */
            ssize_t r = recv(s->fd, &c, 1, MSG_PEEK);
            if (r == 0) s->state = NS_PEERCLOSED;
            else if (r < 0 && errno != EAGAIN) net_fail(s, net_errcode(errno));
        }
        bits |= 8;
    }
    if (s->state == NS_LISTENING && (p.revents & POLLIN)) bits |= 32;
    if ((s->state == NS_CONNECTED || s->state == NS_PEERCLOSED || s->state == NS_BOUND) && (p.revents & POLLOUT)) bits |= 16;
    return s->state | bits;
}

static void net_poll(void)              /* every socket: what changed raises ATTN */
{
    int k;
    for (k = 0; k < NET_NSOCK; k++) {
        struct nsock *s = &net.s[k];
        int now;
        if (s->state == NS_FREE) continue;
        now = net_look(s);
        if (now != s->seen) { s->seen = now; net.events |= 1u << k; net.attn = 1; }
    }
}

/* the Mac's sockets that may change by themselves, for io_wait: their count */
static int net_fds(fd_set *rs, fd_set *ws, int *maxfd)
{
    int k, n = 0;
    if (!net.on) return 0;
    for (k = 0; k < NET_NSOCK; k++) {
        struct nsock *s = &net.s[k];
        if (s->fd < 0 || s->state == NS_FAILED) continue;
        FD_SET(s->fd, rs);
        if (s->state == NS_CONNECTING && s->type == NT_TCP) FD_SET(s->fd, ws);
        if (s->fd > *maxfd) *maxfd = s->fd;
        n++;
    }
    return n;
}

static void net_start_tls(struct nsock *s, uint32_t ip, unsigned port, const char *name)
{
    int sv[2];
    char ips[16], ports[8];
    pid_t pid;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) { net_fail(s, NE_TLS); return; }
    snprintf(ips, sizeof ips, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255);
    snprintf(ports, sizeof ports, "%u", port);
    if ((pid = fork()) == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(sv[1], 0);
        dup2(sv[1], 1);
        if (devnull >= 0) dup2(devnull, 2);
        close(sv[0]);
        execlp("python3", "python3", "-I", net.helper, ips, ports, name, net.ca ? net.ca : "", (char *)0);
        _exit(127);
    }
    close(sv[1]);
    if (pid < 0) { close(sv[0]); net_fail(s, NE_TLS); return; }
    s->fd = sv[0];
    s->pid = pid;
    fcntl(s->fd, F_SETFL, O_NONBLOCK);
    s->state = NS_CONNECTING;
}

static void net_reply(int status, int sock, const uint8_t *p, int n)
{
    net.rep[0] = (uint8_t)status;
    net.rep[1] = (uint8_t)sock;
    net.rep[2] = (uint8_t)n;
    net.rep[3] = (uint8_t)(n >> 8);
    if (n) memcpy(net.rep + 4, p, (size_t)n);
    net.nrep = 4 + n;
    net.prep = 0;
}

static void net_exec(void)
{
    static const uint8_t hello[] = "P32NET 1";
    uint8_t cmd = net.req[0], *pay = net.req + 4, b[NET_MAXPAY];
    int sk = net.req[1], len = net.req[2] | net.req[3] << 8, k;
    struct nsock *s = sk < NET_NSOCK ? &net.s[sk] : NULL;
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    ssize_t r;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    if (cmd >= 0x11 && cmd <= 0x1B && (!s || s->state == NS_FREE)) { net_reply(NE_INVAL, sk, 0, 0); return; }
    switch (cmd) {
    case 0x01:                          /* HELLO */
        for (k = 0; k < NET_NSOCK; k++) net_free(&net.s[k]);
        net.events = 0;
        net.attn = 0;
        net_reply(NE_OK, 0, hello, 8);
        return;
    case 0x02:                          /* IFINFO */
        memset(b, 0, 24);
        b[0] = 1 | 2 | (net.static_ ? 0 : 4);   /* link up, an address, by DHCP unless set */
        b[2] = 0x02; b[3] = 0x50; b[4] = 0x33; b[5] = 0x32; b[6] = 0x00; b[7] = 0x01;
        memcpy(b + 8, net.ifc, 16);
        net_reply(NE_OK, 0, b, 24);
        return;
    case 0x03:                          /* IFCONFIG: kept, and reported back (the Mac's network is unchanged) */
        if (len >= 17 && pay[0] == 1) { memcpy(net.ifc, pay + 1, 16); net.static_ = 1; }
        if (len >= 1 && pay[0] == 0) {  /* DHCP: the network's answer, QEMU's numbers again */
            static const uint8_t dhcp[16] = { 10, 0, 2, 15, 255, 255, 255, 0, 10, 0, 2, 2, 10, 0, 2, 3 };
            memcpy(net.ifc, dhcp, 16);
            net.static_ = 0;
        }
        net_reply(NE_OK, 0, 0, 0);
        return;
    case 0x04:                          /* EVENTS */
        net_poll();
        b[0] = (uint8_t)net.events;
        b[1] = (uint8_t)(net.events >> 8);
        net.events = 0;
        net.attn = 0;
        net_reply(NE_OK, 0, b, 2);
        return;
    case 0x10:                          /* SOCKET */
        if (len < 1 || pay[0] < NT_TCP || pay[0] > NT_ICMP) { net_reply(NE_INVAL, 0, 0, 0); return; }
        for (k = 0; k < NET_NSOCK && net.s[k].state != NS_FREE; k++) ;
        if (k == NET_NSOCK) { net_reply(NE_NOSOCK, 0, 0, 0); return; }
        net_free(&net.s[k]);
        net.s[k].type = pay[0];
        net.s[k].state = NS_NEW;
        net.s[k].seen = NS_NEW;
        net_reply(NE_OK, k, 0, 0);
        return;
    case 0x11:                          /* CONNECT */
        if (s->state != NS_NEW || NET_DGRAM(s->type)) { net_reply(NE_INVAL, sk, 0, 0); return; }
        if (s->type == NT_DNS) {
            struct addrinfo hints, *res = 0;
            char name[256];
            int n = len < 255 ? len : 255;
            memcpy(name, pay, (size_t)n);
            name[n] = 0;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            if (n && getaddrinfo(name, 0, &hints, &res) == 0 && res) {
                uint32_t ip = ntohl(((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr);
                net_p32(s->dns, net_fromhost(ip));
                s->state = NS_CONNECTED;
            } else net_fail(s, NE_NONAME);
            if (res) freeaddrinfo(res);
        } else {
            uint32_t ip;
            unsigned port;
            if (len < 6) { net_reply(NE_INVAL, sk, 0, 0); return; }
            ip = net_g32(pay);
            port = net_g16(pay + 4);
            s->pip = ip;
            s->pport = (uint16_t)port;
            if (ip == net_g32(net.ifc) || (ip >> 24) == 127 || ip == 0) net_fail(s, NE_UNREACH);   /* (no loopback) */
            else if (s->type == NT_TLS) {
                char name[256];
                int n = len - 6 < 255 ? len - 6 : 255;
                memcpy(name, pay + 6, (size_t)n);
                name[n] = 0;
                net_start_tls(s, net_tohost(ip), port, n ? name : "");
            } else if ((s->fd = net_newfd(SOCK_STREAM)) < 0) net_fail(s, NE_NOSOCK);
            else {
                a.sin_addr.s_addr = htonl(net_tohost(ip));
                a.sin_port = htons((unsigned short)port);
                if (connect(s->fd, (struct sockaddr *)&a, sizeof a) == 0) s->state = NS_CONNECTED;
                else if (errno == EINPROGRESS) s->state = NS_CONNECTING;
                else net_fail(s, net_errcode(errno));
            }
        }
        net_poll();                     /* (an answer already: an event) */
        net_reply(NE_OK, sk, 0, 0);
        return;
    case 0x12:                          /* BIND */
        if (len < 2 || s->state != NS_NEW || (s->type != NT_TCP && s->type != NT_UDP)) { net_reply(NE_INVAL, sk, 0, 0); return; }
        {
            unsigned port = net_g16(pay), hport = port && port < 1024 ? port + net.offset : port;
            int one = 1;
            if ((s->fd = net_newfd(s->type == NT_TCP ? SOCK_STREAM : SOCK_DGRAM)) < 0) { net_reply(NE_NOSOCK, sk, 0, 0); return; }
            setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            a.sin_addr.s_addr = htonl(port || s->type == NT_TCP ? INADDR_LOOPBACK : INADDR_ANY);
            a.sin_port = htons((unsigned short)hport);
            if (bind(s->fd, (struct sockaddr *)&a, sizeof a) < 0) {
                int e = errno;
                close(s->fd);
                s->fd = -1;
                net_reply(net_errcode(e), sk, 0, 0);
                return;
            }
            getsockname(s->fd, (struct sockaddr *)&a, &al);
            s->lport = (uint16_t)(port ? port : ntohs(a.sin_port));
            if (s->type == NT_UDP) s->state = NS_BOUND;
        }
        net_reply(NE_OK, sk, 0, 0);
        return;
    case 0x13:                          /* LISTEN */
        if (s->type != NT_TCP || s->state != NS_NEW || s->fd < 0) { net_reply(NE_INVAL, sk, 0, 0); return; }
        if (listen(s->fd, len ? (pay[0] ? pay[0] : 1) : 1) < 0) { net_reply(net_errcode(errno), sk, 0, 0); return; }
        s->state = NS_LISTENING;
        net_reply(NE_OK, sk, 0, 0);
        return;
    case 0x14:                          /* ACCEPT */
        if (s->state != NS_LISTENING) { net_reply(NE_INVAL, sk, 0, 0); return; }
        if (!(net_look(s) & 32)) { net_reply(NE_AGAIN, sk, 0, 0); return; }   /* (nothing waiting) */
        for (k = 0; k < NET_NSOCK && net.s[k].state != NS_FREE; k++) ;
        if (k == NET_NSOCK) { net_reply(NE_NOSOCK, sk, 0, 0); return; }     /* (one waiting, nowhere to put it) */
        {
            int fd = accept(s->fd, (struct sockaddr *)&a, &al), one = 1;
            struct nsock *n = &net.s[k];
            if (fd < 0) { net_reply(errno == EAGAIN || errno == EWOULDBLOCK ? NE_AGAIN : net_errcode(errno), sk, 0, 0); return; }
            fcntl(fd, F_SETFL, O_NONBLOCK);
#ifdef SO_NOSIGPIPE
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            (void)one;
            net_free(n);
            n->type = NT_TCP;
            n->state = NS_CONNECTED;
            n->fd = fd;
            n->lport = s->lport;
            n->pip = net_fromhost(ntohl(a.sin_addr.s_addr));
            n->pport = ntohs(a.sin_port);
            n->seen = -1;
            net_p32(b, n->pip);
            net_p16(b + 4, n->pport);
            net_poll();
            net_reply(NE_OK, k, b, 6);
        }
        return;
    case 0x15:                          /* SEND */
        net_look(s);
        if (s->state == NS_FAILED) { net_reply(s->err, sk, 0, 0); return; }
        if (s->state == NS_CONNECTING) { net_reply(NE_AGAIN, sk, 0, 0); return; }
        if ((s->state != NS_CONNECTED && s->state != NS_PEERCLOSED) || NET_DGRAM(s->type) || s->type == NT_DNS) { net_reply(NE_NOTCONN, sk, 0, 0); return; }
        r = len ? send(s->fd, pay, (size_t)len, 0) : 0;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { net_reply(NE_AGAIN, sk, 0, 0); return; }
        if (r < 0) { int e = net_errcode(errno); net_fail(s, e); net_reply(e, sk, 0, 0); return; }
        b[0] = (uint8_t)r; b[1] = (uint8_t)(r >> 8);
        net_reply(NE_OK, sk, b, 2);
        return;
    case 0x16:                          /* RECV */
        net_look(s);
        if (s->type == NT_DNS) {
            if (s->state == NS_CONNECTED) net_reply(NE_OK, sk, s->dns, 4);
            else net_reply(s->state == NS_FAILED ? s->err : NE_AGAIN, sk, 0, 0);
            return;
        }
        if (s->state == NS_FAILED) { net_reply(s->err, sk, 0, 0); return; }
        if (s->state == NS_CONNECTING) { net_reply(NE_AGAIN, sk, 0, 0); return; }
        if ((s->state != NS_CONNECTED && s->state != NS_PEERCLOSED) || NET_DGRAM(s->type)) { net_reply(NE_NOTCONN, sk, 0, 0); return; }
        {
            int max = len >= 2 ? (int)(pay[0] | pay[1] << 8) : NET_MAXPAY;
            if (max > NET_MAXPAY) max = NET_MAXPAY;
            r = max ? recv(s->fd, b, (size_t)max, 0) : 0;
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { net_reply(NE_AGAIN, sk, 0, 0); return; }
            if (r < 0) { int e = net_errcode(errno); net_fail(s, e); net_reply(e, sk, 0, 0); return; }
            if (r == 0 && max) s->state = NS_PEERCLOSED;
            net_reply(NE_OK, sk, b, (int)r);
        }
        return;
    case 0x17:                          /* SENDTO */
        if (!NET_DGRAM(s->type) || len < 6) { net_reply(NE_INVAL, sk, 0, 0); return; }
        if (s->fd < 0) {                /* not bound: a port of the Mac's choosing; ICMP: the Mac's
                                           unprivileged echo socket (the W5500: IP-raw, protocol 1) */
            if (s->type == NT_ICMP) {
                if ((s->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)) >= 0) fcntl(s->fd, F_SETFL, O_NONBLOCK);
            } else s->fd = net_newfd(SOCK_DGRAM);
            if (s->fd < 0) { net_reply(NE_NOSOCK, sk, 0, 0); return; }
            s->state = NS_BOUND;
        }
        a.sin_addr.s_addr = htonl(net_tohost(net_g32(pay)));
        a.sin_port = htons((unsigned short)net_g16(pay + 4));
        if (s->type == NT_ICMP && len >= 14 && pay[6] == 8) s->icmpid = pay[10] << 8 | pay[11];
        r = sendto(s->fd, pay + 6, (size_t)(len - 6), 0, (struct sockaddr *)&a, sizeof a);
        if (r < 0) { net_reply(errno == EAGAIN ? NE_AGAIN : net_errcode(errno), sk, 0, 0); return; }
        if (!s->lport && getsockname(s->fd, (struct sockaddr *)&a, &al) == 0) s->lport = ntohs(a.sin_port);
        b[0] = (uint8_t)r; b[1] = (uint8_t)(r >> 8);
        net_reply(NE_OK, sk, b, 2);
        return;
    case 0x18:                          /* RECVFROM */
        if (!NET_DGRAM(s->type)) { net_reply(NE_INVAL, sk, 0, 0); return; }
        if (s->fd < 0) { net_reply(NE_AGAIN, sk, 0, 0); return; }
        {
            int max = len >= 2 ? (int)(pay[0] | pay[1] << 8) : NET_MAXPAY - 6;
            if (max > NET_MAXPAY - 6) max = NET_MAXPAY - 6;
            r = recvfrom(s->fd, b + 6, (size_t)max, 0, (struct sockaddr *)&a, &al);
            if (r < 0) { net_reply(errno == EAGAIN || errno == EWOULDBLOCK ? NE_AGAIN : net_errcode(errno), sk, 0, 0); return; }
            if (s->type == NT_ICMP && r >= 20 && (b[6] >> 4) == 4 && (b[6] & 15) * 4 <= r) {    /* (the Mac gives the IP header too: off) */
                int ihl = (b[6] & 15) * 4;
                memmove(b + 6, b + 6 + ihl, (size_t)(r - ihl));
                r -= ihl;
            }
            if (s->type == NT_ICMP && r >= 8 && b[6] == 0 && s->icmpid >= 0 && (b[10] << 8 | b[11]) != s->icmpid) {
                uint32_t sum = 0;       /* (Linux's echo socket sends its own identifier, so the reply
                                           has it; P32W's goes back, and the checksum with it) */
                int k;
                b[10] = (uint8_t)(s->icmpid >> 8); b[11] = (uint8_t)s->icmpid; b[8] = b[9] = 0;
                for (k = 0; k + 1 < r; k += 2) sum += (uint32_t)(b[6 + k] << 8 | b[7 + k]);
                if (r & 1) sum += (uint32_t)(b[6 + r - 1] << 8);
                while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
                b[8] = (uint8_t)(~sum >> 8); b[9] = (uint8_t)~sum;
            }
            net_p32(b, net_fromhost(ntohl(a.sin_addr.s_addr)));
            net_p16(b + 4, ntohs(a.sin_port));
            net_reply(NE_OK, sk, b, 6 + (int)r);
        }
        return;
    case 0x19:                          /* SHUTDOWN */
        if (s->fd >= 0 && !NET_DGRAM(s->type)) shutdown(s->fd, SHUT_WR);
        net_reply(NE_OK, sk, 0, 0);
        return;
    case 0x1A:                          /* CLOSE */
        net_free(s);
        net.events &= ~(1u << sk);
        net_reply(NE_OK, sk, 0, 0);
        return;
    case 0x1B:                          /* STATE */
        {
            int avail = 0, now = net_look(s);
            if (s->fd >= 0 && s->state != NS_LISTENING && ioctl(s->fd, FIONREAD, &avail) < 0) avail = 0;
            if (NET_DGRAM(s->type) && avail <= 0 && (now & 8) && s->fd >= 0) {
                static uint8_t pk[NET_MAXPAY];  /* (Linux's echo socket has no FIONREAD: the datagram's size) */
                ssize_t r = recv(s->fd, pk, sizeof pk, MSG_PEEK | MSG_DONTWAIT);
                avail = r > 0 ? (int)r : 0;
            }
            if (s->state == NS_LISTENING) avail = (now & 32) ? 1 : 0;     /* (a connection waiting) */
            if (s->type == NT_DNS && s->state == NS_CONNECTED) avail = 4;
            if (avail > 65535) avail = 65535;
            memset(b, 0, 14);
            b[0] = (uint8_t)s->state;
            b[1] = (uint8_t)(s->state == NS_FAILED ? s->err : 0);
            b[2] = (uint8_t)avail; b[3] = (uint8_t)(avail >> 8);
            if (now & 16) { b[4] = 0x00; b[5] = 0x08; }        /* room: 2 KiB, a W5500 socket's buffer */
            b[6] = (uint8_t)s->lport; b[7] = (uint8_t)(s->lport >> 8);
            net_p32(b + 8, s->pip);
            b[12] = (uint8_t)s->pport; b[13] = (uint8_t)(s->pport >> 8);
            net_reply(NE_OK, sk, b, 14);
        }
        return;
    }
    net_reply(NE_BADCMD, sk, 0, 0);
}

static uint8_t net_read(unsigned reg)
{
    if (reg == 1) {                     /* NSTAT */
        if (!net.on) return 0;
        if (++net.polls >= 256) { net.polls = 0; net_poll(); }
        return (uint8_t)(NST_READY | (net.prep < net.nrep ? NST_RXF : 0) | NST_TXE |
                         (net.attn ? NST_ATTN : 0) | (net.ien ? NST_IEN : 0));
    }
    if (!net.on || net.prep >= net.nrep) return 0;
    return net.rep[net.prep++];
}

static void net_write(unsigned reg, uint8_t v)
{
    if (reg == 1) {                     /* NCTRL */
        net.ien = (v & NST_IEN) != 0;
        if (v & 0x80) {                 /* RESET: the coprocessor restarts, the latches empty */
            int k;
            for (k = 0; k < NET_NSOCK; k++) net_free(&net.s[k]);
            net.nreq = net.discard = net.nrep = net.prep = 0;
            net.events = 0;
            net.attn = 0;
        }
        return;
    }
    if (!net.on) return;
    if (net.discard) {                  /* the rest of a request that was too long */
        if (--net.discard == 0) net_reply(NE_TOOBIG, net.req[1], 0, 0);
        return;
    }
    net.req[net.nreq++] = v;
    if (net.nreq == 4) {
        int len = net.req[2] | net.req[3] << 8;
        if (len > NET_MAXPAY) { net.discard = len; net.nreq = 0; return; }
    }
    if (net.nreq >= 4 && net.nreq == 4 + (net.req[2] | net.req[3] << 8)) {
        net.nrep = net.prep = 0;
        net_exec();
        net.nreq = 0;
    }
}

static void net_tick(void) { net_poll(); }

static void net_enable(const char *argv0)    /* -N */
{
    net_on_flag = 1;
    const char *sl = strrchr(argv0, '/');
    int k, n = sl ? (int)(sl - argv0) : 0;
    net.on = 1;
    for (k = 0; k < NET_NSOCK; k++) net.s[k].fd = -1;
    if (n) snprintf(net.helper, sizeof net.helper, "%.*s/../tools/p32tls.py", n, argv0);
    else snprintf(net.helper, sizeof net.helper, "../tools/p32tls.py");
    signal(SIGPIPE, SIG_IGN);
}

/* ---------------------------------------------------------------- the video card, revision 2 (D103, D104) */
/* An HD63484 ACRTC and a PS/2 section, as software sees them
   (docs/video-card-acrtc.md): a register page at FFFF_DF00 and nothing
   else -- none of the picture is on the bus.
     +00  halfword  read: the chip's status in bits 7-0 (mask the rest);
                    write: its address register
     +04  halfword  the register addressed, or the FIFOs (address register 0)
     +08  byte      read: the card's status -- bit 0 FULL (a PS/2 byte
                    waits), 1 SRC (0 keyboard, 1 mouse), 2 BUSY, 3 TEXT
                    (text method A's parts: not fitted here), 4 the chip's
                    IRQ; write: a PS/2 command
     +0C  byte      read: the PS/2 byte (reading it clears FULL)
   The chip is p32acrtc.h's model. Its time is the machine's: the timer's
   microseconds, or the microcycles with p32uemu -C, where each access to
   it takes three wait states. The card is fitted only with -V, -K, -Km or
   -dbg (else its window does not answer).

   The emulator draws no picture: -V FILE writes the screen at exit, the
   Base screen's 640 x 480 dots as FILE.pbm and, as FILE, the 25 rows of 80
   characters the kernel's console keeps on lines 40-439: each 8 x 16 cell
   matched against the font's glyphs -- plain, bold, underlined, inverse,
   with the cursor or without -- and '?' where nothing matches.

   The PS/2 section is the ATmega328P with YACC1-D's firmware
   (embedded/video-ps2) as its latches show it. The keyboard's set-2 scan
   codes become ASCII (shift, caps lock, ctrl, the keypad; the arrows and
   the editing keys as VT100 sequences); the mouse's 3-byte packets come
   whole; one queue of 63 bytes, each byte with its source. -K FILE types
   FILE's text on the keyboard (a make and a break per key, with shift or
   ctrl around it); -Km B0,B1,B2 is a packet the mouse sends. What they
   send waits on the wire until the queue has room, so nothing typed is
   lost. Commands (+08): 01 interrupts on, 02 off, 03 acknowledge (no
   interrupt until the next byte), 04 mouse on, 05 off, 06 flush the queue,
   07 keyboard reset, 10-17 the keyboard's LEDs, FF start again.

   The firmware takes its time, and the card's two flip-flops show it
   (D109; "the firmware's time", below): FULL is low for about 50 us after
   each byte is read, a command is taken about 30 us after it is written
   (BUSY until then) and some take long, and after a reset the ATmega is
   two seconds starting. -Kr starts it ready. */
#define WIN_VIDEO   5
#define VID_REG     (WIN(WIN_VIDEO) + 0xF00u)
#define IRQ_PS2     4                   /* the card's level: its PS/2 section and its chip (D52) */
#define VID_TOP     40                  /* the console's first line of the 480, and its rows (kernel/video.c) */
#define VID_ROWS    25

static int vid_fitted;                  /* the card is there only with -V, -K, -Km or -dbg */
static const char *vid_dump;
static unsigned long long vid_ucycle;   /* the microcycle now running (p32uemu sets it) */
static unsigned vid_hz;                 /* the gate-level system's clock for the chip (bm_vid_time), else 0 */

#include "p32acrtc.h"

/* the chip's time is the machine's */
static void vid_time(void)
{
    if (vid_hz) { ac.now = vid_ucycle; ac.hz = vid_hz; }
    else if (t_hw) { ac.now = vid_ucycle; ac.hz = t_hw_div == 4 ? 4000000 : 3000000; }
    else { ac.now = rtc_us; ac.hz = 1000000; }
}
static int vid_chip_irq(void) { vid_time(); return ac_irq(); }
/* The chip's IRQ as the CPU's interrupt logic has it. With the hardware's timing (p32uemu -C) that is the pin as
   the microcycle began (vid_irq_seen: p32uemu sets it): the CPU card samples the -IRQ lines in the middle of a
   bus clock, and the chip takes a register write at that same moment of its cycle's last clock, as CS rises, so
   a request the write causes is first seen in the next microcycle. The gate-level machine shows the same
   (generators/sim_sys.py --video, D107). The card's own registers (the card status, the interrupt status) and
   the acknowledge cycle's vector read the pin itself. */
static int vid_irq_seen;
static int vid_irq_line(void) { return t_hw ? vid_irq_seen : vid_chip_irq(); }
/* until the chip's IRQ with nothing else happening, in the timer's microseconds (0: now; -1: never) */
static long long vid_irq_in(void)
{
    vid_time();
    long long d = ac_irq_in();
    return d <= 0 ? d : (d * 1000000 + (long long)ac.hz - 1) / (long long)ac.hz;
}
/* -C: an access to the chip takes three wait states (docs/video-card-acrtc.md 4.2) */
static inline int vid_chip_access(uint32_t pa, int size)
{
    return vid_fitted && size == 2 && (pa == VID_REG || pa == VID_REG + 4);
}

/* -C: a PS/2 command written (a byte at +08) takes one wait state: the command latch takes the byte as the
   second clock begins, the master still holding it (D106) */
static inline int vid_cmd_access(uint32_t pa, int size)
{
    return vid_fitted && size == 1 && pa == VID_REG + 8;
}

/* ---- the PS/2 section */
#define PW_SIZE 65536u
static uint16_t ps2_wire[PW_SIZE];      /* what the keyboard (0nn) and the mouse (1nn) have still to send */
static unsigned pw_head, pw_tail;
static long long pw_at;                 /* when the oldest of them got there (the card's time) */
static struct {
    uint8_t q[64], src[64];             /* the firmware's queue to the CPU */
    unsigned head, tail;
    int int_on, acked, mouse_on;
    int lshift, rshift, lctrl, rctrl, caps, num, scroll, brk, ext, skip;
    uint8_t mpkt[3];
    int mn;
    /* the card's side: the data latch and the SRC pin as the last load left them, the command latch, the two
       flip-flops; lost: commands written over one not yet taken */
    uint8_t latch, lsrc, cmd;
    int full, busy;
    unsigned lost;
    /* the firmware's time, in the card's ticks (fw_now): its loop is free from `free`; BUSY rose at busy_at; FULL
       was cleared at clear_at; the queue's first byte got there at put_at; +0C was last read at rd_at; setup_at:
       the sketch's setup() is still to come (-1: it has run) */
    long long free, busy_at, clear_at, put_at, rd_at, setup_at;
} fw;

/* The firmware's time (D109). The sketch is YACC1-D's embedded/video-ps2 on an ATmega328P at 16 MHz; these are
   its own delays and the Arduino core's (a digitalRead or digitalWrite 4-5 us), rounded up. The card is not built:
   they are estimates, and software must not depend on them, only on FULL and BUSY.
     FW_FEED  FULL seen clear (or a byte queued) to the next byte in the latch: the loop comes round (to 20 us), 5 us
              of waiting, the pins (25 us)
     FW_TAKE  a command written to the command latch read (BUSY clear): the loop comes round, -RDCMD
     FW_DEV   a command to a PS/2 device and its acknowledge (the request, 11 bits each way at the device's clock)
     FW_BAT   a device's self-test after its reset command
     FW_BOOT  a reset to the sketch's setup(): the start-up time and the bootloader's second (Optiboot) */
#define FW_FEED_US  50
#define FW_TAKE_US  30
#define FW_DEV_US   3000
#define FW_BAT_US   500000
#define FW_BOOT_US  1100000
#define FW_KBD_US   (FW_DEV_US + FW_BAT_US + 2 * FW_DEV_US)        /* keyboard_reset(): reset, self-test, the LEDs */
#define FW_MOUSE_US (FW_DEV_US + FW_BAT_US + 2000 + FW_DEV_US)     /* mouse_start(): reset, self-test, its ID, enable */
#define FW_NEVER    0x7FFFFFFFFFFFFFFFLL
static int fw_ready;                    /* -Kr: the ATmega started long ago (no bootloader, no devices' resets) */

/* the card's time now, and microseconds in its ticks: the microcycles with the hardware's timing (p32uemu -C,
   the gate-level machine), else the timer's microseconds (one an instruction) */
static long long fw_now(void) { vid_time(); return (long long)ac.now; }
static long long fw_t(long long us) { return (us * (long long)ac.hz + 999999) / 1000000; }
static int fw_cycles(void) { return vid_hz || t_hw; }
static long long fw_at;                 /* the turn of the loop that is taking bytes off the wire (fw_pump) */

static void ps2_send(int src, unsigned b)
{
    if (pw_head == pw_tail) pw_at = fw_now();
    if (pw_head - pw_tail < PW_SIZE) ps2_wire[pw_head++ % PW_SIZE] = (uint16_t)(src << 8 | (b & 0xFF));
}
static void ps2_scan(unsigned b) { ps2_send(0, b); }            /* a byte from the keyboard */
static void ps2_mouse(unsigned b) { ps2_send(1, b); }           /* a byte from the mouse */

static int fw_free(void) { return (int)((fw.tail + 64 - fw.head - 1) % 64); }
static void fw_put(unsigned b, int src)
{
    if (!fw_free()) return;
    fw.q[fw.head] = (uint8_t)b; fw.src[fw.head] = (uint8_t)src;
    fw.head = (fw.head + 1) % 64;
    fw.acked = 0;                       /* a new byte asks for attention again */
}

enum { K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN, K_INS, K_DEL };
static void fw_nav(int k)               /* the editing keys as VT100 sequences */
{
    const char *s;
    switch (k) {
    case K_UP: s = "\033[A"; break;     case K_DOWN: s = "\033[B"; break;
    case K_RIGHT: s = "\033[C"; break;  case K_LEFT: s = "\033[D"; break;
    case K_HOME: s = "\033[H"; break;   case K_END: s = "\033[F"; break;
    case K_PGUP: s = "\033[5~"; break;  case K_PGDN: s = "\033[6~"; break;
    case K_DEL: fw_put(0x7F, 0); return;
    default: return;
    }
    if (fw_free() < 4) return;          /* a sequence goes in whole or not at all */
    while (*s) fw_put((uint8_t)*s++, 0);
}

/* a byte from the keyboard: the firmware's keyboard_byte() */
static void fw_key(unsigned c)
{
    static const uint8_t kmap[][3] = {  /* set-2 code, unshifted, shifted */
        {0x0D, 9, 9}, {0x0E, '`', '~'}, {0x15, 'q', 'Q'}, {0x16, '1', '!'}, {0x1A, 'z', 'Z'},
        {0x1B, 's', 'S'}, {0x1C, 'a', 'A'}, {0x1D, 'w', 'W'}, {0x1E, '2', '@'}, {0x21, 'c', 'C'},
        {0x22, 'x', 'X'}, {0x23, 'd', 'D'}, {0x24, 'e', 'E'}, {0x25, '4', '$'}, {0x26, '3', '#'},
        {0x29, ' ', ' '}, {0x2A, 'v', 'V'}, {0x2B, 'f', 'F'}, {0x2C, 't', 'T'}, {0x2D, 'r', 'R'},
        {0x2E, '5', '%'}, {0x31, 'n', 'N'}, {0x32, 'b', 'B'}, {0x33, 'h', 'H'}, {0x34, 'g', 'G'},
        {0x35, 'y', 'Y'}, {0x36, '6', '^'}, {0x3A, 'm', 'M'}, {0x3B, 'j', 'J'}, {0x3C, 'u', 'U'},
        {0x3D, '7', '&'}, {0x3E, '8', '*'}, {0x41, ',', '<'}, {0x42, 'k', 'K'}, {0x43, 'i', 'I'},
        {0x44, 'o', 'O'}, {0x45, '0', ')'}, {0x46, '9', '('}, {0x49, '.', '>'}, {0x4A, '/', '?'},
        {0x4B, 'l', 'L'}, {0x4C, ';', ':'}, {0x4D, 'p', 'P'}, {0x4E, '-', '_'}, {0x52, '\'', '"'},
        {0x54, '[', '{'}, {0x55, '=', '+'}, {0x5A, 13, 13}, {0x5B, ']', '}'},
        {0x5D, '\\', '|'}, {0x66, 8, 8}, {0x76, 27, 27}, {0x7C, '*', '*'}, {0x7B, '-', '-'},
        {0x79, '+', '+'},
    };
    static const uint8_t kpad[][3] = {  /* the keypad: code, the digit (Num Lock on), the editing key (off) */
        {0x70, '0', K_INS}, {0x69, '1', K_END}, {0x72, '2', K_DOWN}, {0x7A, '3', K_PGDN},
        {0x6B, '4', K_LEFT}, {0x73, '5', K_NONE}, {0x74, '6', K_RIGHT}, {0x6C, '7', K_HOME},
        {0x75, '8', K_UP}, {0x7D, '9', K_PGUP}, {0x71, '.', K_DEL} };
    unsigned i, ch = 0;
    int up, e, shift;
    if (fw.skip) { fw.skip--; return; }
    if (c == 0xE1) { fw.skip = 7; return; }             /* Pause / Break: E1 14 77 E1 F0 14 F0 77 */
    if (c == 0xE0) { fw.ext = 1; return; }
    if (c == 0xF0) { fw.brk = 1; return; }
    if (c == 0xAA || c == 0xFA || c == 0xEE || c >= 0xFC) { fw.brk = fw.ext = 0; return; }   /* BAT, acks, errors */
    up = fw.brk; e = fw.ext;
    fw.brk = fw.ext = 0;
    if (c == 0x12 && e) return;                         /* Print Screen's fake shifts */
    if (c == 0x12) { fw.lshift = !up; return; }
    if (c == 0x59 && !e) { fw.rshift = !up; return; }
    if (c == 0x14) { if (e) fw.rctrl = !up; else fw.lctrl = !up; return; }
    if (c == 0x11) return;                              /* Alt: no meaning here */
    if (up) return;                                     /* other releases: nothing */
    if (c == 0x58 || c == 0x77 || (c == 0x7E && !e)) {  /* a lock key: its LED (send_leds(): two commands) */
        if (c == 0x58) fw.caps = !fw.caps;
        else if (c == 0x77) fw.num = !fw.num;
        else fw.scroll = !fw.scroll;
        fw.free = fw_at = fw_at + fw_t(2 * FW_DEV_US);  /* (the loop goes no further meanwhile) */
        return;
    }
    if (e) {
        switch (c) {
        case 0x75: fw_nav(K_UP); return;    case 0x72: fw_nav(K_DOWN); return;
        case 0x6B: fw_nav(K_LEFT); return;  case 0x74: fw_nav(K_RIGHT); return;
        case 0x6C: fw_nav(K_HOME); return;  case 0x69: fw_nav(K_END); return;
        case 0x7D: fw_nav(K_PGUP); return;  case 0x7A: fw_nav(K_PGDN); return;
        case 0x71: fw_nav(K_DEL); return;
        case 0x4A: fw_put('/', 0); return;              /* keypad / */
        case 0x5A: fw_put(13, 0); return;               /* keypad Enter */
        default: return;
        }
    }
    for (i = 0; i < sizeof kpad / sizeof kpad[0]; i++)
        if (kpad[i][0] == c) { if (fw.num) fw_put(kpad[i][1], 0); else fw_nav(kpad[i][2]); return; }
    shift = fw.lshift || fw.rshift;
    for (i = 0; i < sizeof kmap / sizeof kmap[0]; i++)
        if (kmap[i][0] == c) { ch = kmap[i][shift ? 2 : 1]; break; }
    if (!ch) return;
    if (fw.caps && ch >= 'a' && ch <= 'z') ch -= 32;
    else if (fw.caps && shift && ch >= 'A' && ch <= 'Z') ch += 32;
    if ((fw.lctrl || fw.rctrl) && ch >= '@' && ch <= '~') ch &= 0x1F;
    fw_put(ch, 0);
}

static void fw_mouse(unsigned b)        /* a byte from the mouse: its packets, whole */
{
    if (!fw.mouse_on) return;
    if (fw.mn == 0 && !(b & 0x08)) return;              /* byte 1 always has bit 3 set */
    fw.mpkt[fw.mn++] = (uint8_t)b;
    if (fw.mn == 3) {
        fw.mn = 0;
        if (fw_free() >= 3) { fw_put(fw.mpkt[0], 1); fw_put(fw.mpkt[1], 1); fw_put(fw.mpkt[2], 1); }
    }
}

/* what waits on the wire goes through the firmware while its queue has room, once its loop runs */
static void fw_pump(long long now)
{
    long long t = pw_at > fw.free ? pw_at : fw.free;
    int was;
    if (pw_head == pw_tail || fw_free() < 4 || fw.setup_at >= 0 || t > now)
        return;
    was = fw.head != fw.tail;
    fw_at = t;
    while (pw_head != pw_tail && fw_free() >= 4) {
        unsigned w = ps2_wire[pw_tail++ % PW_SIZE];
        if (w & 0x100) fw_mouse(w & 0xFF); else fw_key(w & 0xFF);
    }
    if (!was && fw.head != fw.tail) fw.put_at = t;
    pw_at = t;
}

/* the firmware's start_all(): its queue, its interrupt, the keyboard's keys; the devices' resets take it a second */
static void fw_start_all(void)
{
    fw.head = fw.tail = 0;
    fw.int_on = fw.acked = 0;
    fw.lshift = fw.rshift = fw.lctrl = fw.rctrl = fw.caps = fw.num = fw.scroll = fw.brk = fw.ext = fw.skip = 0;
    fw.mouse_on = 1; fw.mn = 0;         /* (the mouse is plugged in, and still) */
}

/* a command, taken at `when`; the ones that talk to a device keep the loop from everything else meanwhile */
static void fw_command(unsigned c, long long when)
{
    if (c == 0x01) fw.int_on = 1;
    else if (c == 0x02) fw.int_on = 0;
    else if (c == 0x03) fw.acked = 1;
    else if (c == 0x04) { fw.mouse_on = 1; fw.mn = 0; fw.free = when + fw_t(FW_MOUSE_US); }
    else if (c == 0x05) { fw.mouse_on = 0; fw.free = when + fw_t(FW_DEV_US); }
    else if (c == 0x06) { fw.head = fw.tail = 0; fw.mn = 0; }   /* (the queue: the byte in the latch stays, and FULL) */
    else if (c == 0x07) {
        fw.lshift = fw.rshift = fw.lctrl = fw.rctrl = fw.caps = fw.num = fw.scroll = fw.brk = fw.ext = fw.skip = 0;
        fw.free = when + fw_t(FW_KBD_US);
    }
    else if ((c & 0xF8) == 0x10) {
        fw.scroll = c & 1; fw.num = (c >> 1) & 1; fw.caps = (c >> 2) & 1;
        fw.free = when + fw_t(2 * FW_DEV_US);
    }
    else if (c == 0xFF) { fw_start_all(); fw.free = when + fw_t(FW_KBD_US + FW_MOUSE_US); }
}

/* What the firmware does next and when: 0 nothing (until the CPU does something), 1 it reads the command latch,
   2 it loads a byte into the data latch, 3 its setup() runs. Its loop does one thing at a time, a command before
   a byte: whichever it can start first, at the later of the loop being free and the reason being there. */
static int fw_next(long long *when)
{
    long long dc = FW_NEVER, dl = FW_NEVER, t;
    if (fw.setup_at >= 0) { *when = fw.setup_at; return 3; }
    if (fw.busy) dc = fw.busy_at > fw.free ? fw.busy_at : fw.free;
    if (!fw.full && fw.head != fw.tail) {
        t = fw.clear_at > fw.put_at ? fw.clear_at : fw.put_at;
        dl = t > fw.free ? t : fw.free;
    }
    if (dc == FW_NEVER && dl == FW_NEVER) return 0;
    if (dc <= dl) { *when = dc + fw_t(FW_TAKE_US); return 1; }
    *when = dl + fw_t(FW_FEED_US);
    return 2;
}

static void fw_do(int k, long long when)
{
    if (k == 3) {                       /* setup(): a command latched before it is read and dropped; then start_all() */
        fw.setup_at = -1;
        fw.busy = 0;
        return;
    }
    fw.free = when;
    if (k == 1) {
        fw.busy = 0;
        fw_command(fw.cmd, when);
        return;
    }
    fw.latch = fw.q[fw.tail]; fw.lsrc = fw.src[fw.tail];
    if (fw_cycles() && fw.rd_at == when - 1)
        return;                         /* the card still clears FULL, the clock after a read of +0C: FULL does not
                                           take, the firmware sees that and loads the byte again */
    fw.full = 1;
    fw.tail = (fw.tail + 1) % 64;
}

/* the firmware up to now */
static void fw_run(void)
{
    long long now = fw_now(), when;
    int k;
    for (;;) {
        fw_pump(now);
        k = fw_next(&when);
        if (!k || when > now) break;
        fw_do(k, when);
    }
}
/* until the firmware next does something by itself, in the timer's microseconds (0: nothing to come) */
static long long ps2_due_in(void)
{
    long long when, now;
    fw_run();
    now = fw_now();
    if (pw_head != pw_tail && fw_free() >= 4 && fw.setup_at < 0)
        when = pw_at > fw.free ? pw_at : fw.free;       /* (what waits on the wire, at the loop's next turn) */
    else if (!fw_next(&when))
        return 0;
    if (when <= now) when = now + 1;
    return ((when - now) * 1000000 + (long long)ac.hz - 1) / (long long)ac.hz;
}

static int ps2_full(void) { fw_run(); return fw.full; }
static int ps2_intreq(void) { return ps2_full() && fw.int_on && !fw.acked; }
/* the CPU reads +0C: the latch, whatever FULL says, and FULL is cleared */
static unsigned ps2_data(void)
{
    fw_run();
    fw.rd_at = fw_now();
    if (fw.full) { fw.full = 0; fw.clear_at = fw.rd_at; }
    return fw.latch;
}
/* the CPU writes +08: into the command latch, over a command not yet taken; BUSY */
static void ps2_command(unsigned c)
{
    fw_run();
    if (fw.busy) fw.lost++;
    else fw.busy_at = fw_now();
    fw.cmd = (uint8_t)c;
    fw.busy = 1;
}
/* A reset: the card clears FULL (not BUSY: that flip-flop has no reset, and the sketch's setup() reads the
   command latch for it); the ATmega starts again, its bootloader first, then the devices' resets. */
static void ps2_start(void)
{
    long long now = fw_now();
    fw_start_all();
    fw.full = 0;
    fw.clear_at = fw.put_at = fw.rd_at = -2;
    if (fw_ready) { fw.setup_at = -1; fw.busy = 0; fw.free = now; }
    else { fw.setup_at = now + fw_t(FW_BOOT_US); fw.free = fw.setup_at + fw_t(FW_KBD_US + FW_MOUSE_US); }
}

/* -K: one character as the keys that type it (set 2: a make code, F0 and
   the code for the break; shift 12 and ctrl 14 held around it) */
static void ps2_type(int c)
{
    static const char plain[] = "abcdefghijklmnopqrstuvwxyz1234567890`-=[]\\;',./";
    static const char shifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()~_+{}|:\"<>?";
    static const uint8_t code[] = {
        0x1C, 0x32, 0x21, 0x23, 0x24, 0x2B, 0x34, 0x33, 0x43, 0x3B, 0x42, 0x4B, 0x3A, 0x31, 0x44, 0x4D,
        0x15, 0x2D, 0x1B, 0x2C, 0x3C, 0x2A, 0x1D, 0x22, 0x35, 0x1A,             /* a-z */
        0x16, 0x1E, 0x26, 0x25, 0x2E, 0x36, 0x3D, 0x3E, 0x46, 0x45,             /* 1-9, 0 */
        0x0E, 0x4E, 0x55, 0x54, 0x5B, 0x5D, 0x4C, 0x52, 0x41, 0x49, 0x4A };    /* ` - = [ ] \ ; ' , . / */
    const char *p;
    int k = -1, shift = 0, ctrl = 0;
    if (c == ' ') k = -0x29;
    else if (c == '\n' || c == '\r') k = -0x5A;
    else if (c == '\b' || c == 127) k = -0x66;
    else if (c == '\t') k = -0x0D;
    else if (c == 27) k = -0x76;
    else if (c >= 1 && c <= 26) { k = c - 1; ctrl = 1; }   /* ^A-^Z */
    else if (c && (p = strchr(plain, c)) != NULL) k = (int)(p - plain);
    else if (c && (p = strchr(shifted, c)) != NULL) { k = (int)(p - shifted); shift = 1; }
    if (k == -1)
        return;                         /* (no key types it) */
    uint8_t sc = k < 0 ? (uint8_t)-k : code[k];
    if (shift) ps2_scan(0x12);
    if (ctrl) ps2_scan(0x14);
    ps2_scan(sc);
    ps2_scan(0xF0); ps2_scan(sc);
    if (ctrl) { ps2_scan(0xF0); ps2_scan(0x14); }
    if (shift) { ps2_scan(0xF0); ps2_scan(0x12); }
}

/* the card as a reset leaves it: the chip aborted and stopped (its RAM keeps what it held), the firmware started */
static void vid_reset(void)
{
    ac_reset();
    ps2_start();
}

static int vid_read(uint32_t a, int size, uint32_t *v)
{
    unsigned o = a - VID_REG;
    vid_time();
    if (size == 2 && o == 0x00) { *v = (uint32_t)ac_status(); return 0; }
    if (size == 2 && o == 0x04) { *v = ac_read_data(); return 0; }
    if (size == 1 && o == 0x08) {
        int full = ps2_full();          /* (SRC shows only with FULL: GAL VRD) */
        *v = (uint32_t)((full ? 1 : 0) | (full && fw.lsrc ? 2 : 0) | (fw.busy ? 4 : 0) | (ac_irq() ? 0x10 : 0));
        return 0;
    }
    if (size == 1 && o == 0x0C) { *v = ps2_data(); return 0; }
    return 1;
}

static int vid_write(uint32_t a, int size, uint32_t v)
{
    unsigned o = a - VID_REG;
    vid_time();
    if (size == 2 && o == 0x00) { ac_write_ar((uint16_t)v); return 0; }
    if (size == 2 && o == 0x04) { ac_write_data((uint16_t)v); return 0; }
    if (size == 1 && o == 0x08) { ps2_command(v & 0xFF); return 0; }
    return 1;
}

/* ---- what the screen shows: the Base screen (screen 1), 640 x 480, once the chip is started */
static int vid_shown(void) { return (ac.reg[0x04] & 0x40) && ac_mw(1) > 0; }
static uint16_t vid_word(int line, int w) { return ac_rd(ac_sar(1) + (uint32_t)(line * ac_mw(1) + w)); }

/* line n (0 the top one) as 80 bytes, bit 7 the left-hand dot */
static void vid_line(int n, uint8_t *out)
{
    for (int w = 0; w < 40; w++) {
        uint16_t v = vid_shown() ? vid_word(n, w) : 0;
        for (int h = 0; h < 2; h++) {
            uint8_t b = (uint8_t)(v >> (8 * h)), r = 0;
            for (int k = 0; k < 8; k++)
                if (b >> k & 1) r |= (uint8_t)(0x80 >> k);
            out[2 * w + h] = r;
        }
    }
}

/* the character in the console's cell: its dots matched against the font */
static int vid_char(int row, int col)
{
    uint8_t cell[16];
    int l, blank = 1;
    if (!vid_shown()) return ' ';
    for (l = 0; l < 16; l++) {
        uint16_t v = vid_word(VID_TOP + 16 * row + l, col / 2);
        cell[l] = (uint8_t)(col & 1 ? v >> 8 : v);
        if (cell[l]) blank = 0;
    }
    if (blank) return ' ';
    /* plain first, then plain under the cursor (lines 14 and 15 in XOR), then the other pictures: the cursor on a
       blank cell and a bold underscore are the same dots, and the cursor is the likelier */
    for (int k = 0; k < 16; k++) {
        int cur = k == 1 || k >= 9, at = k < 2 ? 0 : k < 9 ? k - 1 : k - 8;     /* at: bit 0 inverse, 1 underline, 2 bold */
        for (int g = 0x20; g < 0x7F; g++) {
            for (l = 0; l < 16; l++) {
                unsigned b = vfont[(g & 1) * VFONT_HALF + (((g - 0x20) >> 5) * 16 + l) * VFONT_MW + ((g & 31) >> 1)];
                if (at & 4) b |= b << 1;
                if ((at & 2) && l == 15) b = 0xFF;
                if (at & 1) b ^= 0xFF;
                if (cur && l >= 14) b ^= 0xFF;
                if ((b & 0xFF) != cell[l]) break;
            }
            if (l == 16) return g;
        }
    }
    return '?';
}

static void vid_write_dump(void)
{
    FILE *f = fopen(vid_dump, "w");
    if (!f) { perror(vid_dump); return; }
    for (int r = 0; r < VID_ROWS; r++) {
        char line[81];
        int n = 0;
        for (int c = 0; c < 80; c++) {
            line[c] = (char)vid_char(r, c);
            if (line[c] != ' ') n = c + 1;
        }
        fprintf(f, "%.*s\n", n, line);
    }
    fclose(f);
    if (vid_shown()) {                  /* the picture: a PBM image (1 = black ink: the dots lit, inverted) */
        char name[512];
        uint8_t row[80];
        snprintf(name, sizeof name, "%s.pbm", vid_dump);
        if (!(f = fopen(name, "wb"))) { perror(name); return; }
        fprintf(f, "P4\n640 480\n");
        for (int n = 0; n < 480; n++) { vid_line(n, row); fwrite(row, 1, 80, f); }
        fclose(f);
    }
}

/* ---------------------------------------------------------------- the card */
/* Asserted IRQ lines as IPEND sees them (bit n = -IRQn low). */
static uint32_t io_ipend(void)
{
    return (t_pending ? 1u << IRQ_TIMER : 0) |
           (ata_intrq(0) || ata_intrq(1) || ata_intrq(2) || ata_intrq(3) || dma_irq() ? 1u << IRQ_DISK : 0) |
           (uart_irq(&uart[0]) || uart_irq(&uart[2]) ? 1u << IRQ_UART0 : 0) |     /* (shared levels, D52) */
           (uart_irq(&uart[1]) || uart_irq(&uart[3]) ? 1u << IRQ_UART1 : 0) |
           (usbs[0].intr || usbs[1].intr ? 1u << IRQ_USB : 0) | (net.attn && net.ien ? 1u << IRQ_NET : 0) |
           (vid_fitted && (ps2_intreq() || vid_irq_line()) ? 1u << IRQ_PS2 : 0);
}

/* ---------------------------------------------------------------- the real-time clock (D23) */
/* A DS12C887 as software sees it (MC146818 registers): 0 seconds, 2 minutes,
   4 hours, 6 day of week (1 = Sunday), 7 date, 8 month, 9 year, 0x32 century,
   A (bit 7 UIP; DV in bits 6:4), B (bit 7 SET, bit 2 DM: 1 = binary, bit 1:
   1 = 24-hour), C (flags; reads 0: no interrupts are modelled), D (bit 7:
   the battery is good), and battery-backed RAM 0x0E-0x7F. The time is UTC:
   by default the host's clock; with -R EPOCH it starts there and advances
   with emulated time (the timer's 1 MHz ticks), so runs are repeatable. */
static long long rtc_base;              /* the clock = rtc_base + rtc_us / 10^6 */
static int rtc_fixed, rtc_inited;
static uint8_t rtc_idx, rtc_a = 0x20, rtc_b = 0x02, rtc_ram[128];
static struct tm rtc_stage;             /* the time being set (SET = 1) */

static long long rtc_now(void)
{
    if (!rtc_inited) { rtc_inited = 1; if (!rtc_fixed) rtc_base = (long long)time(NULL); }
    return rtc_base + (long long)(rtc_us / 1000000);
}
static uint8_t rtc_out(int v) { return (rtc_b & 4) ? (uint8_t)v : (uint8_t)((v / 10) << 4 | v % 10); }
static int rtc_in(uint8_t v) { return (rtc_b & 4) ? v : (v >> 4) * 10 + (v & 15); }

static uint8_t rtc_read(void)
{
    time_t t = (time_t)rtc_now();
    struct tm tm = *gmtime(&t);
    if (rtc_b & 0x80) tm = rtc_stage;
    switch (rtc_idx) {
    case 0: return rtc_out(tm.tm_sec);
    case 2: return rtc_out(tm.tm_min);
    case 4:
        if (rtc_b & 2) return rtc_out(tm.tm_hour);
        return (uint8_t)(rtc_out(tm.tm_hour % 12 ? tm.tm_hour % 12 : 12) | (tm.tm_hour >= 12 ? 0x80 : 0));
    case 6: return rtc_out(tm.tm_wday + 1);
    case 7: return rtc_out(tm.tm_mday);
    case 8: return rtc_out(tm.tm_mon + 1);
    case 9: return rtc_out(tm.tm_year % 100);
    case 0x32: return rtc_out(tm.tm_year / 100 + 19);
    case 0x0A: return rtc_a & 0x7F;     /* UIP never set: the model updates between accesses */
    case 0x0B: return rtc_b;
    case 0x0C: return 0;
    case 0x0D: return 0x80;
    default: return rtc_ram[rtc_idx & 0x7F];
    }
}

static void rtc_commit(struct tm *tm)
{
    tm->tm_isdst = 0;
    rtc_base = (long long)timegm(tm) - (long long)(rtc_us / 1000000);
    rtc_inited = 1;
}

static void rtc_write(uint8_t v)
{
    time_t t = (time_t)rtc_now();
    struct tm tm = (rtc_b & 0x80) ? rtc_stage : *gmtime(&t);
    int field = 1;
    switch (rtc_idx) {
    case 0: tm.tm_sec = rtc_in(v); break;
    case 2: tm.tm_min = rtc_in(v); break;
    case 4:
        if (rtc_b & 2) tm.tm_hour = rtc_in(v);
        else tm.tm_hour = rtc_in(v & 0x7F) % 12 + ((v & 0x80) ? 12 : 0);
        break;
    case 6: break;                      /* the day of the week follows from the date */
    case 7: tm.tm_mday = rtc_in(v); break;
    case 8: tm.tm_mon = rtc_in(v) - 1; break;
    case 9: tm.tm_year = (tm.tm_year / 100) * 100 + rtc_in(v); break;
    case 0x32: tm.tm_year = (rtc_in(v) - 19) * 100 + tm.tm_year % 100; break;
    case 0x0A: rtc_a = v & 0x7F; field = 0; break;
    case 0x0B:
        if ((v & 0x80) && !(rtc_b & 0x80)) rtc_stage = tm;             /* SET: freeze for setting */
        if (!(v & 0x80) && (rtc_b & 0x80)) { rtc_b = v; rtc_commit(&rtc_stage); return; }
        rtc_b = v;
        return;
    case 0x0C: case 0x0D: field = 0; break;
    default: rtc_ram[rtc_idx & 0x7F] = v; field = 0;
    }
    if (field) {
        if (rtc_b & 0x80) rtc_stage = tm;
        else rtc_commit(&tm);
    }
}

/* the UART whose registers a is in, or -1 */
static int uart_at(uint32_t a)
{
    if (a >= IO_UART0 && a < IO_UART0 + 16) return (int)((a >> 3) & 1);
    if (a >= IO_UART2 && a < IO_UART2 + 16) return 2 + (int)((a >> 3) & 1);
    return -1;
}

/* A card's ID, revision and interrupt status (D52): 0 if a is one of them. */
static int card_reg(uint32_t a, int size, uint32_t *v)
{
    unsigned win = (a - WIN(0)) >> 12, o = a & 0xFFFu, id, isr;
    if (win == WIN_IO) {
        id = ID_IO;
        isr = (t_pending ? 1u : 0) | (uart_irq(&uart[0]) ? 2u : 0) | (uart_irq(&uart[1]) ? 4u : 0) |
              (uart_irq(&uart[2]) ? 8u : 0) | (uart_irq(&uart[3]) ? 0x10u : 0) |
              (usbs[0].intr ? 0x40u : 0) | (usbs[1].intr ? 0x80u : 0) | (net.attn && net.ien ? 0x100u : 0);
    } else if (win == WIN_VIDEO && vid_fitted) {
        id = ID_VIDEO;
        isr = (ps2_intreq() ? 1u : 0) | (vid_chip_irq() ? 2u : 0);        /* the PS/2 section, the chip */
    } else if (win == WIN_DISK) {
        id = ID_DISK;
        isr = 0;
        for (unsigned p = 0; p < 4; p++)
            if (ata_intrq(p)) isr |= 1u << p;
        if (dma_irq()) isr |= 1u << 4;  /* the DMA engine (D75) */
    } else
        return 1;
    if (o == CARD_ID && size == 1) { *v = id; return 0; }
    if (o == CARD_REV && size == 1) { *v = id == ID_DISK ? 1 : id == ID_VIDEO ? 2 : 0; return 0; }   /* (the disk card: 1, its DMA engine; the video card: 2, D103) */
    if (o == CARD_ISR && size == 4) { *v = isr; return 0; }
    return 1;
}

/* The interrupt acknowledge cycle (D57): the vector of the card driving
   level n -- its window * 16 + the lowest asking device on that level (its
   interrupt status bit) -- or FF when no card answers. */
static unsigned io_iack(unsigned n)
{
    static const uint8_t uart_bit[4] = { 1, 2, 3, 4 }, usb_bit[2] = { 6, 7 };
    unsigned i;
    switch (n) {
    case IRQ_TIMER:
        if (t_pending) return WIN_IO * 16 + 0;
        break;
    case IRQ_DISK:
        for (i = 0; i < 4; i++)
            if (ata_intrq(i)) return WIN_DISK * 16 + i;
        if (dma_irq()) return WIN_DISK * 16 + 4;
        break;
    case IRQ_UART0: case IRQ_UART1:     /* UARTs 0 and 2 on IRQ2, 1 and 3 on IRQ3 */
        for (i = n - IRQ_UART0; i < 4; i += 2)
            if (uart_irq(&uart[i])) return WIN_IO * 16 + uart_bit[i];
        break;
    case IRQ_PS2:
        if (vid_fitted && ps2_intreq()) return WIN_VIDEO * 16 + 0;
        if (vid_fitted && vid_chip_irq()) return WIN_VIDEO * 16 + 1;
        break;
    case IRQ_USB:
        for (i = 0; i < 2; i++)
            if (usbs[i].intr) return WIN_IO * 16 + usb_bit[i];
        break;
    case IRQ_NET:
        if (net.attn && net.ien) return WIN_IO * 16 + 8;
        break;
    }
    return 0xFF;
}

/* v: the value right-aligned (the MMU / data card does the lane steering).
   Returns 0, or 1 when nothing answers (no -ACK: BUSERROR). */
static int io_read(uint32_t a, int size, uint32_t *v)
{
    if ((a & 0xFFFu) >= CARD_ID && (a & 0xFFFu) < CARD_ISR + 4)
        return card_reg(a, size, v);
    if (a >= TIM_COUNT && a <= TIM_CTRL + 3) {
        if (size != 4 || (a & 3))
            return 1;
        *v = a == TIM_COUNT ? t_count : a == TIM_RELOAD ? t_reload
           : (uint32_t)((t_enable ? 1 : 0) | (t_pending ? 2 : 0));
        return 0;
    }
    if (a >= IO_DISK + DMA_ADR && a < IO_DISK + DMA_CTL + 4 && !(a & 3))
        return dma_reg_read(a - IO_DISK, size, v);                  /* its DMA engine (D75) */
    if (a >= IO_DISK && a < IO_DISK + 64) {                         /* the disk card (D50) */
        unsigned o = a - IO_DISK, port = o >> 4, r = o & 15;
        if ((size != 1 && !(size == 2 && r == 0)))
            return 1;
        if (!atas[port][0].img && !atas[port][1].img) { *v = 0; return 0; }   /* nothing on the port: the card's pull-downs (D66) */
        ata_on(port, ata_sel[port]);
        if (size == 2 && r == 0) { *v = cf.img ? cf_read16() : 0xFFFF; return 0; }   /* D26 */
        if (r < 8)
            *v = cf_read(r);            /* (an empty adapter: not ready, never busy) */
        else
            *v = r == 14 ? (cf.img ? cf.status : 0x00) : 0xFF;    /* alternate status: INTRQ untouched */
        return 0;
    }
    if (vid_fitted && a >= VID_REG && a < VID_REG + 0xF0) return vid_read(a, size, v);    /* the video card */
    if (size != 1)
        return 1;
    if (uart_at(a) >= 0) { *v = uart_read(&uart[uart_at(a)], a & 7); return 0; }
    if (a == IO_PORTIN)  { *v = port_in; return 0; }
    if (a == IO_PORTOUT) { *v = port_out; return 0; }
    if (a == IO_RTC) { *v = 0xFF; return 0; }               /* (the register number is write-only) */
    if (a == IO_RTC + 1) { *v = rtc_read(); return 0; }
    if (a == IO_USB || a == IO_USB + 1) { usb_on(0); *v = usb_read(a & 1); return 0; }    /* D43, D50 */
    if (a == IO_USB1 || a == IO_USB1 + 1) { usb_on(1); *v = usb_read(a & 1); return 0; }  /* (D50) */
    if (a == IO_NET || a == IO_NET + 1) { *v = net_read(a & 1); return 0; }    /* ... (D45) */
    return 1;
}

static int io_write(uint32_t a, int size, uint32_t v)
{
    uint32_t ignored;
    if ((a & 0xFFFu) >= CARD_ID && (a & 0xFFFu) < CARD_ISR + 4)
        return card_reg(a, size, &ignored);     /* read-only: a write is taken and ignored */
    if (a >= TIM_COUNT && a <= TIM_CTRL + 3) {
        if (size != 4 || (a & 3))
            return 1;
        if (a == TIM_COUNT) { t_count = v; t_wrcnt = 1; }
        else if (a == TIM_RELOAD) {
            if (t_hw) { t_rel_new = v; t_rel_pend = 1; }
            else t_reload = v;
        }
        else { t_enable = v & 1; if (v & 2) t_pending = 0; }   /* write 1 to bit 1 = acknowledge */
        return 0;
    }
    if (a >= IO_DISK + DMA_ADR && a < IO_DISK + DMA_CTL + 4 && !(a & 3))
        return dma_reg_write(a - IO_DISK, size, v);                 /* its DMA engine (D75) */
    if (a >= IO_DISK && a < IO_DISK + 64) {                         /* the disk card (D50) */
        unsigned o = a - IO_DISK, port = o >> 4, r = o & 15;
        if (size == 2 && r == 0) { ata_on(port, ata_sel[port]); cf_write16(v); return 0; }    /* D26 */
        if (size != 1)
            return 1;
        if (r == 6)
            ata_sel[port] = (v >> 4) & 1;   /* DEV: master or slave */
        if (r >= 1 && r <= 6)
            for (int u = 0; u < 2; u++) { ata_on(port, u); cf_write(r, (uint8_t)v); }   /* both drives */
        else if (r == 0 || r == 7) {
            ata_on(port, ata_sel[port]);
            cf_write(r, (uint8_t)v);    /* (device control, +14: not modelled) */
        }
        return 0;
    }
    if (vid_fitted && a >= VID_REG && a < VID_REG + 0xF0) return vid_write(a, size, v);
    if (size != 1)
        return 1;
    if (uart_at(a) >= 0) { uart_write(&uart[uart_at(a)], a & 7, (uint8_t)v); return 0; }
    if (a == IO_PORTIN)  return 0;      /* writes to the input port are ignored (it still answers) */
    if (a == IO_PORTOUT) {
        port_out = (uint8_t)v;
        if (io_trace)
            fprintf(stderr, "  OUTPUT PORT <- %02X\n", port_out);
        if (port_hook)
            port_hook(port_out);
        return 0;
    }
    if (a == IO_RTC) { rtc_idx = (uint8_t)(v & 0x7F); return 0; }
    if (a == IO_RTC + 1) { rtc_write((uint8_t)v); return 0; }
    if (a == IO_USB || a == IO_USB + 1) { usb_on(0); usb_write(a & 1, (uint8_t)v); return 0; }
    if (a == IO_USB1 || a == IO_USB1 + 1) { usb_on(1); usb_write(a & 1, (uint8_t)v); return 0; }
    if (a == IO_NET || a == IO_NET + 1) { net_write(a & 1, (uint8_t)v); return 0; }
    return 1;
}

/* WAIT with nothing pending (B3). Returns 0 once an enabled line is up, 1 if
   nothing can ever wake the CPU. Two devices change by themselves: the
   timer, so jump straight to its expiry; and the video card's chip, which
   finishes its work and may then interrupt (D104), so stop there if that
   comes first. */
static int io_wait(uint32_t imask)
{
    while (dma_busy())                  /* (the DMA engine finishes while the CPU waits) */
        dma_word();
    if (io_ipend() & imask)
        return 0;
    fflush(stdout);                     /* (idle: what P32W wrote reaches a pipe or file now, not at exit) */
    int timer = t_enable && (imask & (1u << IRQ_TIMER));
    long long vdue = vid_fitted && (imask & (1u << IRQ_PS2)) ? vid_irq_in() : -1;
    long long pdue = vid_fitted && (imask & (1u << IRQ_PS2)) ? ps2_due_in() : 0;
    if (pdue > 0 && (vdue <= 0 || pdue < vdue)) vdue = pdue;     /* (the PS/2 firmware loads its next byte by itself too) */
    if (vdue > 0 && (!timer || (unsigned long long)vdue < t_count)) {
        for (long long k = 0; k < vdue; k++)
            io_tick();                  /* (the chip is done before the timer's tick: time moves on to then) */
        idle_ticks += (unsigned long long)vdue;
        return 0;
    }
    if (!timer)
        return 1;
    fd_set rs, ws;
    int maxfd = -1, nnet;
    FD_ZERO(&rs);
    FD_ZERO(&ws);
    nnet = net_fds(&rs, &ws, &maxfd);
    int any = nnet || dbg_fd >= 0, k;
    for (k = 0; k < NUART; k++)
        if (uart[k].fd >= 0 || uart[k].lfd >= 0) any = 1;
    if (any) {
        /* interactive, or the network busy: idle in real time (the timer
           ticks at 1 MHz), but wake as soon as a terminal has input or the
           Mac's sockets change -- not at the next tick */
        struct timeval tv, t0, t1;
        int r;
        long us;
        for (k = 0; k < NUART; k++) {
            int f = uart[k].fd >= 0 ? uart[k].fd : uart[k].lfd;
            if (f >= 0) { FD_SET(f, &rs); if (f > maxfd) maxfd = f; }
        }
        if (dbg_fd >= 0) { FD_SET(dbg_fd, &rs); if (dbg_fd > maxfd) maxfd = dbg_fd; }
        tv.tv_sec = 0;
        tv.tv_usec = t_count;
        gettimeofday(&t0, NULL);
        r = select(maxfd + 1, &rs, &ws, NULL, &tv);
        if (nnet) net_poll();
        if (r > 0 && dbg_fd >= 0 && FD_ISSET(dbg_fd, &rs)) dbg_wake = 1;
        if (r > 0) {
            gettimeofday(&t1, NULL);
            us = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);
            if (us < 1) us = 1;
            if (us < (long)t_count) {   /* input before the tick: time moves on that far */
                for (k = 0; k < NUART; k++)
                    uart[k].poll = 255;                 /* (the next check reads it) */
                t_count -= (uint32_t)us;
                idle_ticks += us;
                rtc_us += us;
                return 0;
            }
        }
    }
    idle_ticks += t_count;
    rtc_us += t_count;
    t_pending = 1;
    t_count = t_reload;
    return 0;
}

/* -a, -A, -T: UART 1; with 2 or 3 after the letter (-a2, -T3): that UART; else -1 */
static int uart_opt(const char *o)
{
    if (o[0] != '-' || !o[1] || !strchr("aAT", o[1])) return -1;
    if (!o[2]) return 1;
    if ((o[2] == '2' || o[2] == '3') && !o[3]) return o[2] - '0';
    return -1;
}

/* Command-line options shared by both emulators; returns the number of
   arguments used (0 if argv[i] is not an I/O option, -1 on error). */
static int io_option(int argc, char **argv, int i)
{
    const char *o = argv[i];
    int un = uart_opt(o);
    if (!strcmp(o, "-w")) {
        for (int k = 0; k < NUART; k++)
            uart[k].pace = 1;
        return 1;
    }
    if (un >= 0 && o[1] == 'T' && i + 1 < argc) {    /* UART 1 (-T), 2 (-T2), 3 (-T3) on a TCP port (localhost) */
        struct sockaddr_in a;
        int one = 1, sfd = socket(AF_INET, SOCK_STREAM, 0);
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)atoi(argv[i + 1]));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (sfd < 0 || bind(sfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(sfd, 1) < 0) {
            perror("-T");
            return -1;
        }
        fcntl(sfd, F_SETFL, O_NONBLOCK);
        uart[un].lfd = sfd;
        fprintf(stderr, "[UART %d: nc localhost %s]\n", un, argv[i + 1]);
        return 2;
    }
    if (!strcmp(o, "-dbg") && i + 1 < argc) {    /* the debug server (p32dbg.h); the video card fitted */
        dbg_port = atoi(argv[i + 1]);
        dbg_console = 1;
        vid_fitted = 1;
        return 2;
    }
    if (!strcmp(o, "-R") && i + 1 < argc) {      /* the clock: a fixed start (seconds since 1970, UTC) */
        rtc_base = strtoll(argv[i + 1], NULL, 0);
        rtc_fixed = rtc_inited = 1;
        return 2;
    }
    if (!strcmp(o, "-P") && i + 1 < argc && nprompts < 12) {
        if (!prompts_set) { nprompts = 0; prompts_set = 1; }
        prompts[nprompts++] = argv[i + 1];
        return 2;
    }
    if ((!strcmp(o, "-U") || !strcmp(o, "-U1")) && i + 1 < argc) {  /* a USB drive plugged in: its image (D43) */
        usb_on(o[2] == '1');
        if (!(usb.img = fopen(argv[i + 1], "r+b"))) { perror(argv[i + 1]); return -1; }
        fseek(usb.img, 0, SEEK_END);
        usb.sectors = (uint32_t)(ftell(usb.img) / 512);
        usb.attached = 1;
        usb_on(0);
        return 2;
    }
    if (!strcmp(o, "-N")) {                      /* the network coprocessor, on the Mac's network (D45) */
        net_enable(argv[0]);
        return 1;
    }
    if (!strcmp(o, "-Nport") && i + 1 < argc) {  /* ... listening ports below 1024 move up by this */
        net.offset = (unsigned)strtoul(argv[i + 1], NULL, 0);
        return 2;
    }
    if (!strcmp(o, "-Nca") && i + 1 < argc) {    /* ... TLS trusts these certificates */
        net.ca = argv[i + 1];
        return 2;
    }
    if (!strcmp(o, "-Km") && i + 1 < argc) {     /* the PS/2 mouse sends this packet: B0,B1,B2 */
        unsigned b[3];
        if (sscanf(argv[i + 1], "%x,%x,%x", &b[0], &b[1], &b[2]) != 3) { fprintf(stderr, "-Km B0,B1,B2 (hex)\n"); return -1; }
        for (int k = 0; k < 3; k++) ps2_mouse(b[k]);
        vid_fitted = 1;
        return 2;
    }
    if (!strcmp(o, "-Kr")) {                     /* the PS/2 section's ATmega started long ago: ready from the reset */
        fw_ready = 1;
        vid_fitted = 1;
        return 1;
    }
    if (!strcmp(o, "-V") && i + 1 < argc) {      /* the video card's screen, written at exit */
        vid_dump = argv[i + 1];
        vid_fitted = 1;
        return 2;
    }
    if (!strcmp(o, "-K") && i + 1 < argc) {      /* the PS/2 keyboard types this file */
        FILE *f = fopen(argv[i + 1], "rb");
        int c;
        if (!f) { perror(argv[i + 1]); return -1; }
        while ((c = getc(f)) != EOF) ps2_type(c);
        fclose(f);
        vid_fitted = 1;
        return 2;
    }
    if (!strcmp(o, "-Uout") && i + 1 < argc) {   /* ... pulled out after N sectors have moved */
        usbs[0].out_after = strtol(argv[i + 1], NULL, 0);
        return 2;
    }
    int dn = !strncmp(o, "-d", 2) && (!o[2] || (o[2] >= '1' && o[2] <= '3' && (!o[3] || (o[3] == 's' && o[2] >= '2' && !o[4]))));
    if (i + 1 >= argc || (!dn && strcmp(o, "-p") && !(un >= 0 && (o[1] == 'a' || o[1] == 'A'))))
        return 0;
    const char *arg = argv[i + 1];
    if (!strcmp(o, "-p")) {
        port_in = (uint8_t)strtoul(arg, NULL, 0);
        return 2;
    }
    if (dn) {                           /* a drive's image, opened read/write: -d (CF 0), -d1, -d2[s], -d3[s] */
        unsigned port = o[2] ? (unsigned)(o[2] - '0') : 0;
        ata_on(port, o[2] && o[3] == 's');
        if (!(cf.img = fopen(arg, "r+b"))) { perror(arg); return -1; }
        fseek(cf.img, 0, SEEK_END);
        cf.sectors = (uint32_t)(ftell(cf.img) / 512);
        cf.status = CF_DRDY | CF_DSC;
        cf.fixed = port >= 2;
        ata_on(0, 0);
        return 2;
    }
    FILE *f = fopen(arg, o[1] == 'a' ? "w" : "rb");
    if (!f) { perror(arg); return -1; }
    if (o[1] == 'a') uart[un].out = f; else uart[un].in = f;
    return 2;
}

static void io_init(int trace)
{
    io_trace = trace;
    vid_reset();
    if (vid_dump)
        atexit(vid_write_dump);
    uart[0].in = dbg_console ? NULL : stdin;
    uart[0].out = dbg_console ? NULL : stdout;
    if (!dbg_console && !uart[0].pace && isatty(0) && tcgetattr(0, &tty_saved) == 0) {
        struct termios raw = tty_saved;         /* interactive: keys straight to UART 0 */
        raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | INLCR);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
        tty_raw = 1;
        atexit(io_restore);
        fcntl(0, F_SETFL, O_NONBLOCK);
        uart[0].fd = 0;
        setvbuf(stdout, NULL, _IONBF, 0);
        fprintf(stderr, "[p32emu: interactive; ^] stops the emulator]\n");
    }
    for (unsigned p = 0; p < 4; p++)
        for (int u = 0; u < 2; u++)
            if (!atas[p][u].img)
                atas[p][u].status = 0;
}

#define IO_USAGE \
    "  -d IMG  CompactFlash image (read/write, 512-byte sectors)\n" \
    "  -d1 IMG the second CF socket; -d2 IMG, -d2s IMG IDE A's master, slave; -d3, -d3s IDE B\n" \
    "  -a OUT  UART 1 output file      -A IN  UART 1 input file\n" \
    "  -p N    value on the input port (default 0)\n" \
    "  -w      input a line at a time, each after a prompt (# $ > login: Password: >>> ...) -- for scripts\n" \
    "  -P STR  a prompt for -w (the first -P replaces the defaults)\n" \
    "  -T PORT UART 1 on a TCP port of localhost (nc localhost PORT): a second terminal\n" \
    "  -a2 -A2 -T2, -a3 -A3 -T3: the same for UARTs 2 and 3\n" \
    "  -R SECS the real-time clock starts at SECS (since 1970, UTC) and follows emulated time\n" \
    "  -U IMG  a USB drive holding IMG in USB port 0; -Uout N pulls it out after N sectors\n" \
    "  -U1 IMG a USB drive in USB port 1\n" \
    "  -V FILE the video card's screen at exit: its 25 rows of text to FILE, its 640 x 480 dots to FILE.pbm\n" \
    "  -K FILE the PS/2 keyboard types FILE's text      -Km B0,B1,B2  the PS/2 mouse sends this packet (hex)\n" \
    "  -dbg PORT  the debug server on 127.0.0.1:PORT (0: any; tools/p32dbg), the video card fitted\n" \
    "  -N      the network coprocessor, on the Mac's network (10.0.2.2 is the Mac; docs/net-mailbox.md)\n" \
    "  -Nport N  listening ports below 1024 move up by N (default 8000)   -Nca FILE  certificates TLS trusts\n"

#endif
