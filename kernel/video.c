/* video.c -- the video card's screen and its PS/2 keyboard as a terminal
 * (D50; docs/architecture.md 4a): /dev/tty4, the console when
 * the card is fitted -- kernel messages go to it as well as to UART 0.
 *
 * The screen: 25 rows of 80 cells in the card's text RAM, a cell a
 * halfword (the character, then its attribute). Output understands what
 * P32W Unix's programs send (vi, top, BASIC): CR, LF (scrolling at the
 * bottom), BS, TAB, BEL, and the ANSI sequences ESC [ row ; col H, ESC [ J
 * and 2J, ESC [ K, ESC [ n A / B / C / D, ESC [ ... m (0 normal, 1 bright,
 * 4 underline, 7 inverse). A character in the last column leaves the
 * cursor there, and the next one wraps first (as a VT100), so a full line
 * does not scroll the screen.
 *
 * The keyboard: set-2 scan codes, a byte per interrupt (level 4): F0
 * before a key's code is its release, E0 an extended key. Shift, Ctrl and
 * Caps Lock are kept here; the arrows type ESC [ A / B / C / D and Delete
 * DEL, as a terminal's would. The characters go to the line discipline in
 * tty.c (vtinput). */
#include "defs.h"

#define VTEXT   ((volatile ushort *)VIDMEM)
#define VREG(r) IOB(VIDREG + (r))
#define VCTL    0x00
#define VROW    0x02
#define VCOL    0x03
#define KDATA   0x08
#define KSTAT   0x09
#define VIE     0x10
#define ROWS    25
#define COLS    80

int vt_present;
static int row, col, wrap;              /* the cursor; wrap: a character waits to wrap first */
static int attr;                        /* bits 0 inverse, 1 underline, 2 bright */
static int esc, npar, par[2];           /* the escape parser: 0 text, 1 after ESC, 2 in ESC [ */

static void vt_clear(int from, int to)  /* cells from..to-1 (row * COLS + col) blank */
{
    for (; from < to; from++) VTEXT[from] = ' ';
}

static void vt_scroll(void)
{
    volatile uint *w = (volatile uint *)VIDMEM;
    int k;
    for (k = 0; k < (ROWS - 1) * COLS / 2; k++) w[k] = w[k + COLS / 2];
    vt_clear((ROWS - 1) * COLS, ROWS * COLS);
}

static void vt_newline(void)
{
    if (++row == ROWS) { row = ROWS - 1; vt_scroll(); }
}

static void vt_cursor(void)
{
    VREG(VROW) = row;
    VREG(VCOL) = col;
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
        if (par[0] == 2) vt_clear(0, ROWS * COLS);
        else vt_clear(row * COLS + col, ROWS * COLS);
        break;
    case 'K': vt_clear(row * COLS + col, (row + 1) * COLS); break;
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
    VTEXT[row * COLS + col] = c | attr << 8;
    if (col == COLS - 1) wrap = 1;
    else col++;
}

void vt_flush(void) { if (vt_present) vt_cursor(); }

/* ---------------------------------------------------------------- the keyboard */
/* scan code, the character, the character with shift */
static const uchar keymap[] = {
    0x1C, 'a', 'A', 0x32, 'b', 'B', 0x21, 'c', 'C', 0x23, 'd', 'D', 0x24, 'e', 'E',
    0x2B, 'f', 'F', 0x34, 'g', 'G', 0x33, 'h', 'H', 0x43, 'i', 'I', 0x3B, 'j', 'J',
    0x42, 'k', 'K', 0x4B, 'l', 'L', 0x3A, 'm', 'M', 0x31, 'n', 'N', 0x44, 'o', 'O',
    0x4D, 'p', 'P', 0x15, 'q', 'Q', 0x2D, 'r', 'R', 0x1B, 's', 'S', 0x2C, 't', 'T',
    0x3C, 'u', 'U', 0x2A, 'v', 'V', 0x1D, 'w', 'W', 0x22, 'x', 'X', 0x35, 'y', 'Y',
    0x1A, 'z', 'Z',
    0x16, '1', '!', 0x1E, '2', '@', 0x26, '3', '#', 0x25, '4', '$', 0x2E, '5', '%',
    0x36, '6', '^', 0x3D, '7', '&', 0x3E, '8', '*', 0x46, '9', '(', 0x45, '0', ')',
    0x0E, '`', '~', 0x4E, '-', '_', 0x55, '=', '+', 0x54, '[', '{', 0x5B, ']', '}',
    0x5D, '\\', '|', 0x4C, ';', ':', 0x52, '\'', '"', 0x41, ',', '<', 0x49, '.', '>',
    0x4A, '/', '?', 0x29, ' ', ' ', 0x5A, '\r', '\r', 0x66, '\b', '\b', 0x0D, '\t', '\t',
    0x76, 27, 27,
    0
};
static int brk, ext, shift, ctrl, caps;

static void kbd_char(int c) { vtinput(c); }

static void kbd_byte(int b)
{
    int k, c;
    if (b == 0xF0) { brk = 1; return; }
    if (b == 0xE0) { ext = 1; return; }
    if (b == 0x12 || b == 0x59) shift = !brk;           /* left, right shift */
    else if (b == 0x14) ctrl = !brk;                     /* left ctrl (E0 14: right) */
    else if (b == 0x58) { if (!brk) caps = !caps; }      /* caps lock */
    else if (!brk && ext) {
        switch (b) {
        case 0x75: kbd_char(27); kbd_char('['); kbd_char('A'); break;  /* up */
        case 0x72: kbd_char(27); kbd_char('['); kbd_char('B'); break;  /* down */
        case 0x74: kbd_char(27); kbd_char('['); kbd_char('C'); break;  /* right */
        case 0x6B: kbd_char(27); kbd_char('['); kbd_char('D'); break;  /* left */
        case 0x71: kbd_char(127); break;                               /* delete */
        case 0x5A: kbd_char('\r'); break;                              /* keypad enter */
        case 0x4A: kbd_char('/'); break;                               /* keypad / */
        }
    } else if (!brk) {
        for (k = 0; keymap[k]; k += 3)
            if (keymap[k] == b) {
                c = keymap[k + (shift ? 2 : 1)];
                if (caps && c >= 'a' && c <= 'z') c -= 32;
                else if (caps && c >= 'A' && c <= 'Z' && shift) c += 32;
                if (ctrl && (c | 32) >= 'a' && (c | 32) <= 'z') c &= 31;
                kbd_char(c);
                break;
            }
    }
    brk = ext = 0;
}

void kbdintr(void)
{
    while (VREG(KSTAT) & 1)
        kbd_byte(VREG(KDATA));
}

/* the card there? (its ID, read with a probe: an empty window is a BUSERROR) */
int video_init(void)
{
    if (probe8(VIDREG + 0xF0) != 3) return -1;
    vt_present = 1;
    vt_clear(0, ROWS * COLS);
    VREG(VCTL) = 1 | 8;                 /* text and the cursor on */
    vt_cursor();
    VREG(VIE) = 1;                      /* keyboard interrupts */
    mtcr_imask(mfcr_imask() | 1 << IRQ_PS2);
    return 0;
}
