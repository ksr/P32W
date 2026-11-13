/* video.c -- the video card's screen and its PS/2 keyboard as a terminal
 * (D53; the card: docs/video-card-acrtc.md, D103, D104): /dev/tty4, the
 * console when the card is fitted -- kernel messages go to it as well as to
 * UART 0.
 *
 * The card is an HD63484 ACRTC: the picture, 640 x 480 dots, is in the
 * chip's own memory, and nothing of it is on the bus. The chip draws from
 * commands, 16-bit words written to its write FIFO (eight words; a word
 * more is lost, so they go only while the chip says it has room). The
 * screen is 25 rows of 80 cells of 8 x 16 dots on lines 40-439.
 *
 *   A character is one graphic copy (AGCPY) of its 8 x 16 dots from a
 *     picture of the font kept under the screen. The copy leaves the chip's
 *     pointer at the next cell, so a run of characters needs no move.
 *   The font's picture comes from the card's EPROM at start. Seven more are
 *     made from it by the chip: bold (for bright), underlined, inverse and
 *     their mixes; the attribute picks the picture.
 *   A scroll is one copy (CPY) and one clear (CLR); the chip is busy about
 *     30 ms on it. Clearing a line or the screen is a CLR.
 *   The cursor is lines 14 and 15 of its cell in XOR, drawn when output
 *     stops and taken away before the next.
 *
 * The words wait in a ring here. The chip's "write FIFO empty" interrupt
 * (the card's device 1) moves them on, eight at a time, so nothing waits in
 * a loop while the chip draws; kernel messages (kprintf) do wait, as they
 * must work with interrupts off and in a panic.
 *
 * Output understands what P32W Unix's programs send (vi, top, BASIC): CR,
 * LF (scrolling at the bottom), BS, TAB, BEL, and the ANSI sequences ESC [
 * row ; col H, ESC [ J and 2J, ESC [ K, ESC [ n A / B / C / D, ESC [ ... m
 * (0 normal, 1 bright, 4 underline, 7 inverse). A character in the last
 * column leaves the cursor there, and the next one wraps first (as a
 * VT100), so a full line does not scroll the screen.
 *
 * The keyboard: the card's PS/2 section (an ATmega328P) sends ASCII, the
 * arrows and editing keys as ESC [ sequences, a byte per interrupt (the
 * card's device 0): the firmware puts the next byte in the card's latch
 * some 50 us after one is read, so the handler finds one byte, and FULL
 * asks again for the next. The characters go to the line discipline in
 * tty.c (vtinput). The mouse is turned off: the keyboard alone is the
 * console's. The firmware's "acknowledge" (03) is not used: after it bytes
 * the firmware already holds come with no interrupt at all.
 *
 * /dev/video (D110) lends the chip to one program: what it writes are the
 * chip's command words, as they are; what it reads are the words a command
 * put in the chip's read FIFO. While it is open the console draws nothing:
 * its text goes into vtext only, a copy of the 25 rows that is kept all
 * the time. When the program closes the device, or dies, the chip is
 * stopped in whatever it was doing, its drawing registers and the font's
 * pictures are made again (the program may have written anywhere in the
 * chip's memory), and the text is drawn again from vtext. */
#include "defs.h"
#include <sys/video.h>

#define VH(r)   (*(volatile ushort *)(VIDREG + (r)))
#define VSTAT   0x00            /* read: the chip's status, in its low byte */
#define VAR     0x00            /* write: the chip's address register */
#define VDATA   0x04            /* the register addressed; with the address register 0, the FIFOs */
#define PSTAT   0x08            /* read: bit 0 FULL (a byte waits), 1 SRC (1 = the mouse's), 2 BUSY */
#define PCMD    0x08            /* write: a command to the PS/2 section */
#define PDATA   0x0C
#define WFE     0x01            /* the chip's status: the write FIFO is empty */
#define WFR     0x02            /* ... it has room */
#define RFR     0x04            /* ... the read FIFO has a word */
#define RFF     0x08            /* ... and is full */
#define CED     0x20            /* ... no command in hand */
#define CCR     0x02            /* the chip's command control register: its low byte, an interrupt for each of those */

#define ROWS    25
#define COLS    80
#define MW      40              /* words a line: the screen's memory width */
#define TOP     40              /* the text's first line of the 480 */
#define ORGW    (479 * MW)      /* the drawing origin: the screen's bottom-left word (the chip's Y runs up) */
#define PICT(a) ((480 + 48 * (a)) * MW)     /* font picture a's first word: under the screen */
#define YTOP(r) (479 - (TOP + 16 * (r)))    /* the Y of row r's top line */
#define FONTROM 0x80000         /* the font EPROM in the chip's memory: the even places, then (+1920) the odd */

int vt_present;
static int row, col, wrap;              /* the cursor; wrap: a character waits to wrap first */
static int attr;                        /* bits 0 inverse, 1 underline, 2 bright: the font picture */
static int esc, npar, par[2];           /* the escape parser: 0 text, 1 after ESC, 2 in ESC [ */
static int cur_on, cur_row, cur_col;    /* the cursor as drawn */
static int cp_ok, cp_row, cp_col;       /* the chip's pointer is at this cell's top-left */

/* What the text is, a word a cell: the character, the font's picture (the attribute) << 8; 0 a blank never
   written. The picture itself is in the chip, so this is what the screen is drawn from again when a program
   has had it (/dev/video). A scroll turns the rows round (vtop) and moves nothing. */
static ushort vtext[ROWS * COLS];
static int vtop;                        /* the row of vtext that is the screen's row 0 */
static uint vgen;                       /* counts the changes (the screen being drawn again looks at it) */
#define CELL(r, c) vtext[((vtop + (r)) % ROWS) * COLS + (c)]
static int vg;                          /* 0 the screen is the console's; 1 a program's; 2 being given back */

/* ---------------------------------------------------------------- words to the chip */
#define VQ      1024
#define BURST   64                      /* room one character may need: a cursor, a scroll, a move, a glyph */
static ushort vq[VQ];
static uint vqh, vqt;
static uint ien;                        /* the chip's interrupts that are on (CCR's low byte) */
static uint vwant;                      /* ... those a sleeping reader of /dev/video asked for, until the next one */
#define VQLEN() (vqh - vqt)

static void vw(uint w) { vq[vqh++ % VQ] = w; }

/* words into the write FIFO while it has room: eight when it is empty */
static void pump(void)
{
    uint st;
    VH(VAR) = 0;
    while (VQLEN() && ((st = VH(VSTAT)) & WFR)) {
        int n = (st & WFE) ? 8 : 1;
        while (n-- && VQLEN()) VH(VDATA) = vq[vqt++ % VQ];
    }
}

/* the chip interrupts on "write FIFO empty" while words wait here, and on what a reader waits for */
static void arm(void)
{
    uint m = (VQLEN() ? WFE : 0) | vwant;
    if (m == ien) return;
    ien = m;
    VH(VAR) = CCR;
    VH(VDATA) = m;
}

static void drain(uint room)            /* wait here until the ring has room (kernel messages, the start) */
{
    while (VQ - VQLEN() < room) pump();
}

/* room for a character's words? A program has the screen: always (the text goes to vtext only); the screen is
   being given back: no, the terminal's output waits a moment */
int vt_room(void) { return vg ? vg == 1 : VQ - VQLEN() >= BURST; }

static void vroom(uint n)               /* sleep until the ring has room for n words (a process's own time) */
{
    int s = splhi();
    while (VQ - VQLEN() < n) {
        pump();
        arm();
        if (VQ - VQLEN() >= n) break;
        sleep(vq);
    }
    splx(s);
}

/* ---------------------------------------------------------------- the chip's commands */
static void wpr(int r, uint v) { vw(0x0800 | r); vw(v); }                  /* a drawing parameter register */
static void rwp(uint a) { wpr(0x0C, 0x4000 | a >> 12); wpr(0x0D, (a << 4) & 0xFFFF); }    /* the read / write pointer: a word of the Base screen */
static void amove(int x, int y) { vw(0x8000); vw(x & 0xFFFF); vw(y & 0xFFFF); cp_ok = 0; }
static void clr(int words, int lines) { vw(0x5800); vw(0); vw(words - 1); vw(-(lines - 1) & 0xFFFF); }   /* from the pointer, going down */
static void cpy(int op, uint from, int words, int lines)                   /* to the pointer, going down */
{
    vw(op); vw(from >> 12); vw((from << 4) & 0xFFFF);
    vw(words - 1); vw(-(lines - 1) & 0xFFFF);
}

static void glyph_draw(int r, int c, int ch, int at)
{
    if (!cp_ok || cp_row != r || cp_col != c) amove(8 * c, YTOP(r));
    vw(0xED00);                                             /* AGCPY: the source down its columns, then right */
    vw((ch & 31) * 8); vw((-1 - 48 * at - 16 * ((ch - 0x20) >> 5)) & 0xFFFF);
    vw(7); vw(-15 & 0xFFFF);
    cp_ok = 1; cp_row = r; cp_col = c + 1;
}

static void glyph_at(int r, int c, int ch, int at)
{
    if (ch < 0x20 || ch > 0x7E) ch = 0x7F;                  /* (no glyph: blank) */
    CELL(r, c) = ch | at << 8;
    vgen++;
    if (!vg) glyph_draw(r, c, ch, at);
}

static void cursor_xor(int r, int c)
{
    amove(8 * c, YTOP(r) - 14);
    vw(0xC003); vw(8 * c + 7); vw((YTOP(r) - 15) & 0xFFFF);   /* AFRCT in XOR */
}

static void clear_rows(int r, int n)    /* n whole rows from row r */
{
    int k;
    if (n <= 0) return;
    for (k = r; k < r + n; k++) memset(&CELL(k, 0), 0, COLS * sizeof(ushort));
    vgen++;
    if (vg) return;
    rwp((TOP + 16 * r) * MW);
    clr(MW, 16 * n);
}

static void clear_eol(int r, int c)     /* row r from column c to its end */
{
    memset(&CELL(r, c), 0, (COLS - c) * sizeof(ushort));
    vgen++;
    if (vg) return;
    if (c & 1) glyph_draw(r, c++, ' ', 0);                  /* (a CLR is whole words: two cells) */
    if (c < COLS) {
        rwp((TOP + 16 * r) * MW + c / 2);
        clr((COLS - c) / 2, 16);
    }
}

static void vt_scroll(void)
{
    vtop = (vtop + 1) % ROWS;                               /* (the old top row is the new last one: blank) */
    memset(&CELL(ROWS - 1, 0), 0, COLS * sizeof(ushort));
    vgen++;
    if (vg) return;
    rwp(TOP * MW);
    cpy(0x6100, (TOP + 16) * MW, MW, 16 * (ROWS - 1));      /* rows 1-24 up a row; the pointer ends at the last */
    clr(MW, 16);
}

static void vt_newline(void)
{
    if (++row == ROWS) { row = ROWS - 1; vt_scroll(); }
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void vt_csi(int c)               /* the end of ESC [ params c */
{
    int n = par[0] ? par[0] : 1, k;
    wrap = 0;
    switch (c) {
    case 'H': case 'f': row = clamp(n - 1, 0, ROWS - 1); col = clamp((par[1] ? par[1] : 1) - 1, 0, COLS - 1); break;
    case 'A': row = clamp(row - n, 0, ROWS - 1); break;
    case 'B': row = clamp(row + n, 0, ROWS - 1); break;
    case 'C': col = clamp(col + n, 0, COLS - 1); break;
    case 'D': col = clamp(col - n, 0, COLS - 1); break;
    case 'J':
        if (par[0] == 2) clear_rows(0, ROWS);
        else { clear_eol(row, col); clear_rows(row + 1, ROWS - 1 - row); }
        break;
    case 'K': clear_eol(row, col); break;
    case 'm':
        for (k = 0; k < (npar ? npar : 1); k++)
            switch (par[k]) {
            case 0: attr = 0; break;
            case 1: attr |= 4; break;
            case 4: attr |= 2; break;
            case 7: attr |= 1; break;
            case 22: attr &= ~4; break;
            case 24: attr &= ~2; break;
            case 27: attr &= ~1; break;
            }
        break;
    }                                   /* (anything else: ignored) */
}

void vt_putc(int c)
{
    c &= 0xFF;
    if (!vt_present) return;
    if (!vg && !vt_room()) drain(BURST);    /* (tty.c asks vt_room() first; a kernel message waits here) */
    if (cur_on) { cursor_xor(cur_row, cur_col); cur_on = 0; }
    if (esc == 1) {
        if (c == '[') { esc = 2; npar = 0; par[0] = par[1] = 0; return; }
        esc = 0;                        /* (ESC and anything else: dropped) */
        return;
    }
    if (esc == 2) {
        if (c >= '0' && c <= '9') {
            if (npar == 0) npar = 1;
            if (npar <= 2) par[npar - 1] = par[npar - 1] * 10 + c - '0';
            return;
        }
        if (c == ';') { if (npar == 0) npar = 1; npar++; return; }
        if (c == '?') return;           /* (private modes: ignored) */
        esc = 0;
        vt_csi(c);
        return;
    }
    switch (c) {
    case 27: esc = 1; return;
    case '\r': col = 0; wrap = 0; return;
    case '\n': wrap = 0; vt_newline(); return;
    case '\b': if (col > 0) col--; wrap = 0; return;
    case '\t': col = (col | 7) + 1; if (col >= COLS) col = COLS - 1; return;
    case 7: return;
    }
    if (c < 32) return;
    if (wrap) { col = 0; wrap = 0; vt_newline(); }
    glyph_at(row, col, c, attr);
    if (col == COLS - 1) wrap = 1;
    else col++;
}

static void cursor_show(void)
{
    if (!vt_present || vg || cur_on) return;
    cursor_xor(row, col);
    cur_on = 1; cur_row = row; cur_col = col;
}

/* output stops for now: the cursor, and what the chip takes at once; the rest from its interrupt */
void vt_flush(void)
{
    if (!vt_present || vg) return;
    cursor_show();
    pump();
    arm();
}

/* a kernel message: every word to the chip before going on */
void vt_sync(void)
{
    if (!vt_present || vg) return;      /* (a program has the screen: the message is in vtext, and shows after) */
    cursor_show();
    drain(VQ);
    arm();
}

/* the chip's interrupt (the card's device 1): its write FIFO is empty, or what a reader waited for is there */
void vidintr(void)
{
    if (!vt_present) return;
    pump();
    vwant = 0;                          /* (a reader that still waits asks again) */
    arm();
    wakeup(vq);
    if (vt_room()) vtstart();           /* (tty.c: more of /dev/tty4's output) */
}

/* ---------------------------------------------------------------- the keyboard */
/* The PS/2 section's start (D109): three commands, each written once the firmware has taken the one before
   (BUSY clear), a clock tick at a time, so the kernel never waits for the ATmega. It needs waiting for: after a
   reset it is two seconds starting (its bootloader, then the keyboard's and the mouse's own resets), and a
   command it finds in the latch when its sketch begins it reads and drops. That is what the first one is for:
   dropped or taken, it changes nothing. A card with no ATmega in its socket never clears BUSY: then nothing
   more is written, and the screen works without a keyboard. */
static const uchar ps2start[] = {
    0x02,                               /* interrupts off: as a reset leaves it (may be dropped) */
    0x05,                               /* the mouse off */
    0x01,                               /* a byte waiting interrupts */
};
static uint ps2next = sizeof ps2start;  /* (nothing to send until the card is found) */

void kbdtick(void)
{
    if (ps2next < sizeof ps2start && !(IOB(VIDREG + PSTAT) & 4))
        IOB(VIDREG + PCMD) = ps2start[ps2next++];
}

void kbdintr(void)
{
    uint st;
    while ((st = IOB(VIDREG + PSTAT)) & 1) {            /* read the byte only once FULL shows */
        int c = IOB(VIDREG + PDATA);
        if (!(st & 2)) vtinput(c);                      /* (a mouse's byte: dropped) */
    }
}

/* ---------------------------------------------------------------- start */
static void creg(int r, uint v) { VH(VAR) = r; VH(VDATA) = v; }

/* The chip's drawing state as the console wants it, the screen and the place under it black, the font's eight
   pictures made there: at the start, and again when a program has had the chip. wait(n): until the ring has
   room for n words (the start waits in a loop; a process sleeps). */
static void vt_setup(void (*wait)(uint))
{
    int a, k;
    wait(128);
    vw(0x0400); vw(0x4000 | ORGW >> 12); vw((ORGW << 4) & 0xFFFF);    /* ORG: the origin */
    wpr(0x00, 0x0000); wpr(0x01, 0xFFFF);                              /* the two colours: black, white */
    wpr(0x05, 0x0000); wpr(0x06, 0x0000); wpr(0x07, 0xF0F0);           /* the pattern's pointers */
    vw(0x1800); vw(16);                                                /* WPTN: the pattern RAM solid */
    for (k = 0; k < 16; k++) vw(0xFFFF);
    rwp(0); clr(MW, 480 + 8 * 48);                                     /* the screen and the pictures' place: black */
    wpr(0x04, 0x00FF); rwp(PICT(0));                                   /* the font from the EPROM: the even places */
    cpy(0x7100, FONTROM, 16, 48);
    wpr(0x04, 0xFF00); rwp(PICT(0));                                   /* ... and the odd ones */
    cpy(0x7100, FONTROM + 1920, 16, 48);
    wpr(0x04, 0xFFFF);
    wait(VQ);
    for (a = 1; a < 8; a++) {
        int y0 = -1 - 48 * a;
        rwp(PICT(a)); cpy(0x6100, PICT(0), 16, 48);                    /* a copy of the plain picture */
        if (a & 4) {                                                   /* bold: again a dot to the right, ORed */
            amove(1, y0);
            vw(0xE101); vw(0); vw(-1 & 0xFFFF); vw(254); vw(-47 & 0xFFFF);
        }
        if (a & 2)                                                     /* underline: the last line of each row of glyphs */
            for (k = 0; k < 3; k++) {
                amove(0, y0 - 16 * k - 15);
                vw(0x8800); vw(256); vw((y0 - 16 * k - 15) & 0xFFFF);
            }
        if (a & 1) {                                                   /* inverse: the whole picture in XOR */
            amove(0, y0);
            vw(0xC003); vw(255); vw((y0 - 47) & 0xFFFF);
        }
        wait(VQ);
    }
}

/* the card there? (its ID and revision, read with a probe: an empty window is a BUSERROR) */
int video_init(void)
{
    if (probe8(VIDREG + 0xF0) != 3 || probe8(VIDREG + 0xF1) != 2) return -1;
    creg(CCR, 0x0000);                  /* ABT off (a reset sets it: the FIFOs are shut), one bit a pixel */
    creg(0x04, 0x8088);                 /* OMR: master, stopped, interleaved access */
    creg(0x06, 0x4000);                 /* DCR: the Base screen on; attribute 00: white, both syncs negative */
    creg(0x82, 0x630C);                 /* 640 x 480 at 60 Hz from 25.175 MHz: a line 100 memory cycles, sync 12 */
    creg(0x84, 0x054F);                 /* back porch 6, 80 cycles shown */
    creg(0x86, 0x020D);                 /* 525 lines */
    creg(0x88, 0x2002);                 /* back porch 33, sync 2 */
    creg(0x8A, 0x01E0);                 /* the Base screen: 480 lines */
    creg(0xCA, 0x0028);                 /* ... 40 words a line */
    creg(0xCC, 0x0000);                 /* ... from word 0 */
    creg(0xCE, 0x0000);
    creg(0x04, 0xC088);                 /* start */
    vt_present = 1;
    vt_setup(drain);
    row = col = wrap = attr = 0;
    vt_sync();
    ps2next = 0;                        /* the PS/2 section's commands: from the clock's ticks (kbdtick) */
    mtcr_imask(mfcr_imask() | 1 << IRQ_PS2);
    return 0;
}

/* ---------------------------------------------------------------- /dev/video: the chip for a program (D110) */
int vgopen(int minor, int flags)
{
    int s;
    if (minor || !vt_present) return -ENXIO;
    s = splhi();
    if (vg) { splx(s); return -EBUSY; }             /* one program at a time */
    if (cur_on) { cursor_xor(cur_row, cur_col); cur_on = 0; }
    vg = 1;                                         /* from here the console draws nothing. The screen is as */
    cp_ok = 0;                                      /* it left it (a program may want to read it), and so are */
    pump();                                         /* the chip's drawing registers */
    arm();
    splx(s);
    return 0;
}

/* The screen back to the console. The program's words still here are dropped and the chip's command in hand is
   aborted (ABT also empties its FIFOs); then everything the console depends on is made again, and the text
   drawn from vtext. Text that arrives meanwhile waits (vt_room); a kernel message that does not wait changes
   vtext under the loop, which then goes round again. */
int vgclose(int minor)
{
    int s, r, c;
    uint g, x;
    s = splhi();
    vg = 2;
    vqh = vqt;
    vwant = 0;
    VH(VAR) = CCR; VH(VDATA) = 0x8000;
    VH(VAR) = CCR; VH(VDATA) = 0x0000;
    ien = 0;
    splx(s);
    vt_setup(vroom);
    do {
        g = vgen;
        for (r = 0; r < ROWS; r++)
            for (c = 0; c < COLS; c++) {
                x = CELL(r, c);
                if (!x || x == ' ') continue;       /* (black already) */
                vroom(BURST);
                s = splhi();
                glyph_draw(r, c, x & 0xFF, x >> 8);
                splx(s);
            }
        if (g != vgen) { vroom(BURST); s = splhi(); rwp(TOP * MW); clr(MW, 16 * ROWS); cp_ok = 0; splx(s); }
    } while (g != vgen);
    s = splhi();
    vg = 0;
    cp_ok = cur_on = 0;
    vt_flush();
    splx(s);
    vtstart();                                      /* (the terminal's output that waited) */
    return 0;
}

/* the chip's command words, as they are: into the ring behind whatever is there, sleeping while it is full */
int vgwrite(int minor, uint src, int n, uint off)
{
    ushort w[64];
    int done = 0, k, i, s;
    if (n & 1) return -EINVAL;
    while (done < n) {
        k = n - done < (int)sizeof w ? n - done : (int)sizeof w;
        if (copyin(w, src + done, k) < 0) return done ? done : -EFAULT;
        s = splhi();
        while (VQ - VQLEN() < (uint)k / 2) {
            pump();
            arm();
            if (VQ - VQLEN() >= (uint)k / 2) break;
            if (curproc->killed) { splx(s); return done ? done : -EINTR; }
            sleep(vq);
        }
        for (i = 0; i < k / 2; i++) vw(w[i]);
        pump();
        arm();
        splx(s);
        done += k;
    }
    return n;
}

/* sleep until the chip's flags `flags` (interrupts for them, once) or its write FIFO's turn; 0, or -EINTR */
static int vgsleep(uint flags)
{
    if (curproc->killed) return -EINTR;
    vwant = flags;
    arm();
    sleep(vq);
    return 0;
}

/* Words from the chip's read FIFO (what RD, RPR and RPTN put there): as many as are asked for, or fewer when
   the chip has done everything written and its FIFO is empty: no more will come. */
int vgread(int minor, uint dst, int n, uint off)
{
    ushort w[8];
    int done = 0, k, s;
    uint st;
    if (n & 1) return -EINVAL;
    while (done < n) {
        s = splhi();
        for (;;) {
            pump();
            st = VH(VSTAT);
            if (st & RFR) break;
            if (!VQLEN() && (st & (CED | WFE)) == (CED | WFE)) { splx(s); return done; }
            if (vgsleep(RFR | CED) < 0) { splx(s); return done ? done : -EINTR; }
        }
        VH(VAR) = 0;
        for (k = 0; k < 8 && done + 2 * k < n && (VH(VSTAT) & RFR); k++) w[k] = VH(VDATA);
        splx(s);
        if (copyout(dst + done, w, 2 * k) < 0) return -EFAULT;
        done += 2 * k;
    }
    return done;
}

int vgioctl(int minor, int cmd, uint arg)
{
    struct vidinfo vi;
    int s;
    uint st;
    if (cmd == VIOCINFO) {
        vi.width = 8 * COLS; vi.height = 480; vi.memw = MW;
        vi.cellw = 8; vi.cellh = 16;
        vi.fonts = 8; vi.fonty = -1; vi.fontstep = 48;
        return copyout(arg, &vi, sizeof vi) < 0 ? -EFAULT : 0;
    }
    if (cmd != VIOCSYNC) return -EINVAL;
    s = splhi();                                    /* until the ring is empty and the chip has nothing in hand */
    for (;;) {
        pump();
        st = VH(VSTAT);
        if (!VQLEN() && (st & (CED | WFE)) == (CED | WFE)) break;
        if (st & RFF) { splx(s); return -EBUSY; }   /* (the chip waits for its read FIFO to be read) */
        if (vgsleep(CED | RFF) < 0) { splx(s); return -EINTR; }
    }
    splx(s);
    return 0;
}
