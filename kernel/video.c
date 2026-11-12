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
 * card's device 0). The characters go to the line discipline in tty.c
 * (vtinput). The mouse is turned off: the keyboard alone is the console's. */
#include "defs.h"

#define VH(r)   (*(volatile ushort *)(VIDREG + (r)))
#define VSTAT   0x00            /* read: the chip's status, in its low byte */
#define VAR     0x00            /* write: the chip's address register */
#define VDATA   0x04            /* the register addressed; with the address register 0, the FIFOs */
#define PSTAT   0x08            /* read: bit 0 FULL (a byte waits), 1 SRC (1 = the mouse's), 2 BUSY */
#define PCMD    0x08            /* write: a command to the PS/2 section */
#define PDATA   0x0C
#define WFE     1               /* the chip's status: the write FIFO is empty */
#define WFR     2               /* ... it has room */
#define CCR     0x02            /* the chip's command control register: bit 0 WEE, interrupt on WFE */

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

/* ---------------------------------------------------------------- words to the chip */
#define VQ      1024
#define BURST   64                      /* room one character may need: a cursor, a scroll, a move, a glyph */
static ushort vq[VQ];
static uint vqh, vqt;
static int wee;                         /* the chip's interrupt on "write FIFO empty" is on */
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

static void set_wee(int on)
{
    if (on == wee) return;
    wee = on;
    VH(VAR) = CCR;
    VH(VDATA) = on ? 1 : 0;
}

static void drain(uint room)            /* wait here until the ring has room (kernel messages, the start) */
{
    while (VQ - VQLEN() < room) pump();
}

int vt_room(void) { return VQ - VQLEN() >= BURST; }

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

static void glyph_at(int r, int c, int ch, int at)
{
    if (ch < 0x20 || ch > 0x7E) ch = 0x7F;                  /* (no glyph: blank) */
    if (!cp_ok || cp_row != r || cp_col != c) amove(8 * c, YTOP(r));
    vw(0xED00);                                             /* AGCPY: the source down its columns, then right */
    vw((ch & 31) * 8); vw((-1 - 48 * at - 16 * ((ch - 0x20) >> 5)) & 0xFFFF);
    vw(7); vw(-15 & 0xFFFF);
    cp_ok = 1; cp_row = r; cp_col = c + 1;
}

static void cursor_xor(int r, int c)
{
    amove(8 * c, YTOP(r) - 14);
    vw(0xC003); vw(8 * c + 7); vw((YTOP(r) - 15) & 0xFFFF);   /* AFRCT in XOR */
}

static void clear_rows(int r, int n)    /* n whole rows from row r */
{
    if (n <= 0) return;
    rwp((TOP + 16 * r) * MW);
    clr(MW, 16 * n);
}

static void clear_eol(int r, int c)     /* row r from column c to its end */
{
    if (c & 1) glyph_at(r, c++, ' ', 0);                    /* (a CLR is whole words: two cells) */
    if (c < COLS) {
        rwp((TOP + 16 * r) * MW + c / 2);
        clr((COLS - c) / 2, 16);
    }
}

static void vt_scroll(void)
{
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
    if (!vt_room()) drain(BURST);       /* (tty.c asks vt_room() first; a kernel message waits here) */
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
    if (!vt_present || cur_on) return;
    cursor_xor(row, col);
    cur_on = 1; cur_row = row; cur_col = col;
}

/* output stops for now: the cursor, and what the chip takes at once; the rest from its interrupt */
void vt_flush(void)
{
    if (!vt_present) return;
    cursor_show();
    pump();
    set_wee(VQLEN() != 0);
}

/* a kernel message: every word to the chip before going on */
void vt_sync(void)
{
    if (!vt_present) return;
    cursor_show();
    drain(VQ);
    set_wee(0);
}

/* the chip's write FIFO is empty (the card's device 1) */
void vidintr(void)
{
    if (!vt_present) return;
    pump();
    set_wee(VQLEN() != 0);
    if (vt_room()) vtstart();           /* (tty.c: more of /dev/tty4's output) */
}

/* ---------------------------------------------------------------- the keyboard */
static void ps2cmd(int c)
{
    while (IOB(VIDREG + PSTAT) & 4) ;   /* (BUSY: the last command not yet taken) */
    IOB(VIDREG + PCMD) = c;
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

/* the card there? (its ID and revision, read with a probe: an empty window is a BUSERROR) */
int video_init(void)
{
    int a, k;
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
    drain(VQ);
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
        drain(VQ);
    }
    row = col = wrap = attr = 0;
    vt_sync();
    ps2cmd(0x05);                       /* the mouse off */
    ps2cmd(0x01);                       /* a byte waiting interrupts */
    mtcr_imask(mfcr_imask() | 1 << IRQ_PS2);
    return 0;
}
