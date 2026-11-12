/*
 * acrtc_test.c -- the emulators' model of the HD63484 (../p32acrtc.h) by
 * itself, on the chip's 16-bit host bus, driven as the kernel's console
 * drives it (kernel/video.c; docs/video-card-acrtc.md). run_tests.sh builds
 * and runs it:   cc -std=c99 -I.. -o acrtc_test acrtc_test.c && ./acrtc_test
 *
 * Checked: the chip after reset (ABT set, the FIFOs shut, status 23); the
 * registers as words and the address register's steps of 2; the start
 * sequence; the font's picture copied from the EPROM by two masked copies;
 * the seven other pictures (bold, underline, inverse and their mixes) made
 * by the chip; characters by AGCPY, in every attribute, as dots on the
 * screen; clearing by CLR; the scroll by CPY; the cursor in XOR; the write
 * FIFO's eight words, a ninth lost, WFR and WFE; the read FIFO (RD, RPR);
 * the pattern RAM in words (WPTN, RPTN); IRQ from CCR's enables and when it
 * will come; a command the model lacks (CER) and ABT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "p32acrtc.h"

static int fails, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { fails++; printf("FAIL: %s\n", what); }
}

/* ---- the driver's side: a word to the write FIFO once the chip has room (time moves on to then) */
static unsigned long waits;
static void fifo(unsigned w)
{
    ac_write_ar(0);
    if (!(ac_status() & 2)) { ac.now = ac.free_at; waits++; }
    ac_write_data((uint16_t)w);
}
static void idle(void) { if (ac.now < ac.free_at) ac.now = ac.free_at; (void)ac_status(); }
static void reg(unsigned r, unsigned v) { ac_write_ar((uint16_t)r); ac_write_data((uint16_t)v); }
static unsigned rreg(unsigned r) { ac_write_ar((uint16_t)r); return ac_read_data(); }

#define MW      40
#define ORGW    (479 * MW)              /* the origin: the screen's bottom-left word */
#define ROWS    25
#define TOP     40                      /* the text's first line of the 480 */
#define PICT(a) ((480 + 48 * (a)) * MW) /* font picture a's first word: under the screen */

static void wpr(unsigned r, unsigned v) { fifo(0x0800 | r); fifo(v); }
static void rwp(unsigned long a) { wpr(0x0C, 0x4000 | (unsigned)(a >> 12)); wpr(0x0D, (unsigned)(a << 4) & 0xFFFF); }
static void amove(int x, int y) { fifo(0x8000); fifo((unsigned)x & 0xFFFF); fifo((unsigned)y & 0xFFFF); }
static int ytop(int row) { return 479 - (TOP + 16 * row); }

static void start(void)
{
    int a, k;
    reg(0x02, 0x0000);                  /* CCR: ABT off, one bit a pixel, no interrupts */
    reg(0x04, 0x8088);                  /* OMR: master, stopped, interleaved access */
    reg(0x06, 0x4000);                  /* DCR: the Base screen on, ATR 00 */
    reg(0x82, 0x630C); reg(0x84, 0x054F); reg(0x86, 0x020D); reg(0x88, 0x2002); reg(0x8A, 0x01E0);
    reg(0xCA, 0x0028); reg(0xCC, 0x0000); reg(0xCE, 0x0000);
    reg(0x04, 0xC088);                  /* start */
    fifo(0x0400); fifo(0x4000 | ORGW >> 12); fifo((ORGW << 4) & 0xFFFF);      /* ORG */
    wpr(0x00, 0x0000); wpr(0x01, 0xFFFF);                                    /* CL0, CL1 */
    wpr(0x05, 0x0000); wpr(0x06, 0x0000); wpr(0x07, 0xF0F0);                 /* the pattern's pointers */
    fifo(0x1800); fifo(16);                                                  /* WPTN: the pattern RAM solid */
    for (k = 0; k < 16; k++) fifo(0xFFFF);
    rwp(0); fifo(0x5800); fifo(0); fifo(MW - 1); fifo((unsigned)-(480 + 8 * 48 - 1) & 0xFFFF);    /* CLR: screen and pictures */
    wpr(0x04, 0x00FF); rwp(PICT(0));                                         /* the font: the even places */
    fifo(0x7100); fifo(0x0080); fifo(0x0000); fifo(15); fifo((unsigned)-47 & 0xFFFF);
    wpr(0x04, 0xFF00); rwp(PICT(0));                                         /* ... and the odd ones */
    fifo(0x7100); fifo(0x0080); fifo((VFONT_HALF << 4) & 0xFFFF); fifo(15); fifo((unsigned)-47 & 0xFFFF);
    wpr(0x04, 0xFFFF);
    for (a = 1; a < 8; a++) {
        int y0 = -1 - 48 * a;
        rwp(PICT(a));                                                        /* a copy of the plain picture */
        fifo(0x6100); fifo(PICT(0) >> 12); fifo((PICT(0) << 4) & 0xFFFF); fifo(15); fifo((unsigned)-47 & 0xFFFF);
        if (a & 4) {                                                         /* bold: the picture again a dot to the right, ORed */
            amove(1, y0);
            fifo(0xE101); fifo(0); fifo((unsigned)-1 & 0xFFFF); fifo(254); fifo((unsigned)-47 & 0xFFFF);
        }
        if (a & 2)                                                           /* underline: each row's last line */
            for (k = 0; k < 3; k++) { amove(0, y0 - 16 * k - 15); fifo(0x8800); fifo(256); fifo((unsigned)(y0 - 16 * k - 15) & 0xFFFF); }
        if (a & 1) {                                                         /* inverse: the whole picture in XOR */
            amove(0, y0);
            fifo(0xC003); fifo(255); fifo((unsigned)(y0 - 47) & 0xFFFF);
        }
    }
}

static void putch(int row, int col, int c, int attr)
{
    amove(8 * col, ytop(row));
    fifo(0xED00); fifo((unsigned)(c & 31) * 8); fifo((unsigned)(-1 - 48 * attr - 16 * ((c - 0x20) >> 5)) & 0xFFFF);
    fifo(7); fifo((unsigned)-15 & 0xFFFF);
}
static void cursor(int row, int col)
{
    amove(8 * col, ytop(row) - 14);
    fifo(0xC003); fifo((unsigned)(8 * col + 7)); fifo((unsigned)(ytop(row) - 15) & 0xFFFF);
}
static void scroll(void)
{
    rwp(TOP * MW);
    fifo(0x6100); fifo(((TOP + 16) * MW) >> 12); fifo((((TOP + 16) * MW) << 4) & 0xFFFF);
    fifo(MW - 1); fifo((unsigned)-(16 * (ROWS - 1) - 1) & 0xFFFF);
    fifo(0x5800); fifo(0); fifo(MW - 1); fifo((unsigned)-15 & 0xFFFF);
}

/* ---- what the frame buffer holds: a line of a cell (bit 0 the left-hand dot), and what a glyph should be */
static unsigned cell_line(int row, int col, int l)
{
    uint16_t w = ac_ram[(TOP + 16 * row + l) * MW + col / 2];
    return col & 1 ? w >> 8 : w & 0xFF;
}
static unsigned glyph_line(int c, int l, int attr)
{
    unsigned b = vfont[(c & 1) * VFONT_HALF + (((c - 0x20) >> 5) * 16 + l) * VFONT_MW + ((c & 31) >> 1)];
    if (attr & 4) b |= b << 1;
    if ((attr & 2) && l == 15) b = 0xFF;
    if (attr & 1) b ^= 0xFF;
    return b & 0xFF;
}
static int cell_is(int row, int col, int c, int attr)
{
    for (int l = 0; l < 16; l++)
        if (cell_line(row, col, l) != glyph_line(c, l, attr)) return 0;
    return 1;
}

int main(void)
{
    int k, a;
    char msg[96];
    (void)ac_sar; (void)ac_raster;      /* (the card's side of the model: p32io.h uses them) */
    ac_reset();

    /* reset: ABT set, the FIFOs shut; the status is 23 (CED, WFR, WFE) under a high byte of ones */
    check(ac_status() == 0xFF23, "status after reset is not FF23");
    ac_write_ar(0); ac_write_data(0x5800);
    check(ac.ncmd == 0 && ac.fifo == 0, "a word reached the write FIFO with ABT set");
    check(rreg(0x02) == 0x8000, "CCR after reset is not 8000");

    /* registers as words; r80 up step the address register by 2, r02-r7F do not; its bit 0 reads as 0 */
    reg(0x06, 0x1234);
    check(rreg(0x06) == 0x1234 && rreg(0x07) == 0x1234, "a register below r80 does not read back, or the address register's bit 0 counts");
    ac_write_ar(0x82); ac_write_data(0x630C); ac_write_data(0x054F);
    ac_write_ar(0x82);
    check(ac_read_data() == 0x630C && ac_read_data() == 0x054F && ac.ar == 0x86, "r82, r84 do not step the address register by 2");

    start();
    idle();
    check(ac.lost == 0 && ac.missing == 0 && !ac.cer, "the start sequence lost a word or used a command the model lacks");
    check(waits > 0, "the start sequence never had to wait for the write FIFO");
    check((ac_status() & 0xFF) == 0x23, "the chip is not idle after the start sequence");

    /* the font's picture in RAM: every glyph, plain and in the seven other pictures */
    for (a = 0; a < 8; a++)
        for (k = 0x20; k < 0x80; k++)
            for (int l = 0; l < 16; l++) {
                uint16_t w = ac_ram[PICT(a) + (((k - 0x20) >> 5) * 16 + l) * MW + ((k & 31) >> 1)];
                unsigned got = k & 1 ? w >> 8 : w & 0xFF;
                if (got != glyph_line(k, l, a)) {
                    snprintf(msg, sizeof msg, "font picture %d: glyph %02X line %d is %02X, not %02X", a, k, l, got, glyph_line(k, l, a));
                    check(0, msg);
                    a = 8; k = 0x80; break;
                }
            }
    check(1, "");
    for (k = 0; k < 480 * MW; k++) if (ac_ram[k]) break;
    check(k == 480 * MW, "the screen is not clear after the start sequence");

    /* characters, in every attribute, at even and odd columns, first and last rows */
    for (a = 0; a < 8; a++) {
        putch(0, 2 * a, 'A' + a, a); putch(ROWS - 1, 79 - 2 * a, 'g', a);
    }
    idle();
    for (a = 0; a < 8; a++) {
        snprintf(msg, sizeof msg, "a character with attribute %d is not its glyph on the screen", a);
        check(cell_is(0, 2 * a, 'A' + a, a) && cell_is(ROWS - 1, 79 - 2 * a, 'g', a), msg);
        check(cell_is(0, 2 * a + 1, ' ', 0), "a character spilled into the cell to its right");
    }
    /* a run of characters needs one AMOVE: the copy leaves the pointer at the next cell */
    amove(8 * 10, ytop(3));
    for (k = 0; k < 5; k++) {
        fifo(0xED00); fifo((unsigned)("Hello"[k] & 31) * 8); fifo((unsigned)(-1 - 16 * (("Hello"[k] - 0x20) >> 5)) & 0xFFFF);
        fifo(7); fifo((unsigned)-15 & 0xFFFF);
    }
    idle();
    for (k = 0; k < 5; k++) check(cell_is(3, 10 + k, "Hello"[k], 0), "a run of characters after one AMOVE");
    check(ac.cx == 8 * 15 && ac.cy == ytop(3), "the current pointer after a run is not at the next cell");

    /* the cursor in XOR, drawn and taken away */
    cursor(3, 11); idle();
    check(cell_line(3, 11, 14) == (glyph_line('e', 14, 0) ^ 0xFF) && cell_line(3, 11, 15) == (glyph_line('e', 15, 0) ^ 0xFF) &&
          cell_line(3, 11, 13) == glyph_line('e', 13, 0) && cell_is(3, 10, 'H', 0) && cell_is(3, 12, 'l', 0), "the cursor: lines 14 and 15 of its cell in XOR");
    cursor(3, 11); idle();
    check(cell_is(3, 11, 'e', 0), "the cursor taken away leaves the character");

    /* clear to the end of a line from an even column: one CLR of whole words */
    rwp((TOP + 16 * 3) * MW + 12 / 2); fifo(0x5800); fifo(0); fifo((80 - 12) / 2 - 1); fifo((unsigned)-15 & 0xFFFF); idle();
    check(cell_is(3, 11, 'e', 0) && cell_is(3, 12, ' ', 0) && cell_is(3, 14, ' ', 0) && cell_is(3, 79, ' ', 0), "CLR from column 12 to the line's end");

    /* the scroll: every row up one, the last row blank, the pictures under the screen untouched */
    putch(1, 0, 'x', 0); putch(ROWS - 1, 5, 'y', 1);
    scroll(); idle();
    check(cell_is(0, 0, 'x', 0) && cell_is(2, 10, 'H', 0) && cell_is(ROWS - 2, 5, 'y', 1) && cell_is(ROWS - 2, 79, 'g', 0), "the scroll moved the rows up");
    for (k = 0; k < 80; k++) if (!cell_is(ROWS - 1, k, ' ', 0)) break;
    check(k == 80, "the last row is not blank after the scroll");
    for (k = 0; k < TOP * MW; k++) if (ac_ram[k]) break;
    check(k == TOP * MW && !ac_ram[(TOP + 16 * ROWS) * MW], "the scroll touched the bands above or below the text");
    check(ac_ram[PICT(0) + 16 * MW] == (uint16_t)(glyph_line('@', 0, 0) | glyph_line('A', 0, 0) << 8), "the scroll touched the font's picture");

    /* the write FIFO: the chip busy on a long CLR, eight words wait, the ninth is lost; then the chip is free */
    idle();
    rwp(0); idle();
    ac_write_ar(0);
    ac_write_data(0x5800); ac_write_data(0); ac_write_data(MW - 1); ac_write_data((unsigned)-39 & 0xFFFF);   /* 40 lines: the top band */
    check((ac_status() & 0x23) == 0x03, "busy with its command taken: the FIFO empty (WFE, WFR) and no CED wanted");
    for (k = 0; k < 8; k++) ac_write_data(0x8000);                    /* (AMOVE's op-code, eight times: eight words waiting) */
    check((ac_status() & 0x03) == 0x00, "eight words waiting: WFR should be 0");
    {
        unsigned long lost = ac.lost;
        ac_write_data(0x8000);
        check(ac.lost == lost + 1, "a ninth word was not lost");
    }
    check(ac_irq_in() == -1, "IRQ expected with no enable set");
    reg(0x02, 0x0001);                                                /* WEE: interrupt on write FIFO empty */
    check(!ac_irq() && ac_irq_in() == (long long)(ac.free_at - ac.now) && ac_irq_in() > 0, "IRQ is not due when the chip will be free");
    ac.now = ac.free_at;
    check(ac_irq() && ac_irq_in() == 0 && (ac_status() & 0x03) == 0x03, "free again: WFE, WFR and IRQ wanted");
    reg(0x02, 0x8000); reg(0x02, 0x0000);                             /* ABT: the half-sent AMOVEs dropped */
    check((ac_status() & 0xFF) == 0x23 && !ac_irq(), "ABT: status 23 and no IRQ wanted");

    /* the read FIFO: RD steps the pointer; RPR; RFR and its interrupt */
    rwp(ORGW + 3); fifo(0x4800); fifo(0xBEEF); fifo(0x4800); fifo(0x1234);   /* WT, WT */
    rwp(ORGW + 3); fifo(0x4400); fifo(0x4400); idle();                         /* RD, RD */
    check((ac_status() & 0x0C) == 0x04, "RFR without RFF wanted after two RDs");
    reg(0x02, 0x0004); check(ac_irq(), "RRE: IRQ wanted while the read FIFO holds a word"); reg(0x02, 0x0000);
    ac_write_ar(0);
    check(ac_read_data() == 0xBEEF && ac_read_data() == 0x1234 && !(ac_status() & 0x04), "RD did not give the two words written");
    check(ac_read_data() == 0xFFFF, "the empty read FIFO should read as the floating bus");
    fifo(0x0C0D); idle(); ac_write_ar(0);                                      /* RPR Pr0D: the pointer's low part, two words on */
    check(ac_read_data() == (((ORGW + 5) << 4) & 0xFFFF), "RPR of the read / write pointer after two RDs");
    rwp(ORGW + 3); fifo(0x4800); fifo(0); fifo(0x4800); fifo(0); idle();       /* (the two words back to 0) */

    /* the pattern RAM, in words on this bus */
    fifo(0x1802); fifo(3); fifo(0x1111); fifo(0x2222); fifo(0x3333);           /* WPTN from word 2, three words */
    fifo(0x1C02); fifo(3); idle(); ac_write_ar(0);                             /* RPTN */
    check(ac_read_data() == 0x1111 && ac_read_data() == 0x2222 && ac_read_data() == 0x3333, "WPTN and RPTN in words");

    /* a command the model lacks sets CER, and ABT clears it */
    fprintf(stderr, "(the next line is the model saying what it lacks: wanted)\n");
    fifo(0xA800); fifo(10); idle();                                            /* CRCL */
    check((ac_status() & 0x80) && ac.missing == 1, "CRCL should set CER");
    reg(0x02, 0x8000); reg(0x02, 0x0000);
    check(!(ac_status() & 0x80), "ABT should clear CER");

    /* time: a character is busy for the manual's count, twice over in dual access */
    idle();
    {
        unsigned long long t0 = ac.now;
        fifo(0xED00); fifo(8); fifo((unsigned)-17 & 0xFFFF); fifo(7); fifo((unsigned)-15 & 0xFFFF);
        unsigned long long us = ac.free_at - t0;
        snprintf(msg, sizeof msg, "a character takes %llu us of the chip's time (about 292 wanted: 918 2CLK cycles, doubled)", us);
        check(us >= 285 && us <= 300, msg);
    }

    printf("acrtc_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
