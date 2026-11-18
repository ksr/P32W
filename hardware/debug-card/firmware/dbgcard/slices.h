/* slices.h -- which slice and bit of the debug card each line is (docs/debug-card-theory.md section 7).
 * Written by generators/card_debug.py --firmware from its SLICES: do not edit (make test checks it is current).
 * A line's place is slice << 3 | bit; 0xFF: the card has no such line. */
#ifndef DBG_SLICES_H
#define DBG_SLICES_H
#include <stdint.h>

#define NSLICE 22                       /* slices 0-21: a sample latch, a capture SRAM, a read buffer each */
#define NDRIVE 19                       /* slices 0-18 have a drive register too */

static const uint8_t L_D[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
static const uint8_t L_A[32] = {0xFF, 0xFF, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D};
static const uint8_t L_BE[4] = {0x3E, 0x3F, 0x40, 0x41};
static const uint8_t L_LR[32] = {0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F};

/* slice  0 (D): D0 D1 D2 D3 D4 D5 D6 D7 */
/* slice  1 (D): D8 D9 D10 D11 D12 D13 D14 D15 */
/* slice  2 (D): D16 D17 D18 D19 D20 D21 D22 D23 */
/* slice  3 (D): D24 D25 D26 D27 D28 D29 D30 D31 */
/* slice  4 (BUS): A2 A3 A4 A5 A6 A7 A8 A9 */
/* slice  5 (BUS): A10 A11 A12 A13 A14 A15 A16 A17 */
/* slice  6 (BUS): A18 A19 A20 A21 A22 A23 A24 A25 */
/* slice  7 (BUS): A26 A27 A28 A29 A30 A31 BE0 BE1 */
/* slice  8 (BUS): BE2 BE3 -MRD -MWR -IACK */
/* slice  9 (SYS): -RES -BREQ3 -BREQ4 -BREQ5 -BREQ6 -BREQ7 -BREQ8 -BREQ9 */
/* slice 10 (SYS): -BREQ10 CLK -CLK -FAST */
/* slice 11 (GNT): -BGNT3 -BGNT4 -BGNT5 -BGNT6 -BGNT7 -BGNT8 -BGNT9 -BGNT10 */
/* slice 12 (LR): LR0 LR1 LR2 LR3 LR4 LR5 LR6 LR7 */
/* slice 13 (LR): LR8 LR9 LR10 LR11 LR12 LR13 LR14 LR15 */
/* slice 14 (LR): LR16 LR17 LR18 LR19 LR20 LR21 LR22 LR23 */
/* slice 15 (LR): LR24 LR25 LR26 LR27 LR28 LR29 LR30 LR31 */
/* slice 16 (J3): RSRC0 RSRC1 RSRC2 RSRC3 RDST0 RDST1 RDST2 MEM0 */
/* slice 17 (J3): MEM1 MEM2 SIZE0 SIZE1 SIGNED SUPER TLBW CRSEL0 */
/* slice 18 (J3): CRSEL1 CRSEL2 -WSTB LOCKREQ PCINC MARPC MARINC CCLK */
/* slice 19 (listened to only): PWROK -LOCK -IRQ0 -IRQ1 -IRQ2 -IRQ3 -IRQ4 -IRQ5 */
/* slice 20 (listened to only): -IRQ6 -IRQ7 -BREQ -BGNT -ACK -WAIT FAULT FC0 */
/* slice 21 (listened to only): FC1 FC2 FC3 STALL -CPUIN -MMUIN */

#define L_MRDN       0x42
#define L_MWRN       0x43
#define L_IACKN      0x44
#define L_RESN       0x48
#define L_BREQ3N     0x49
#define L_BREQ4N     0x4A
#define L_BREQ5N     0x4B
#define L_BREQ6N     0x4C
#define L_BREQ7N     0x4D
#define L_BREQ8N     0x4E
#define L_BREQ9N     0x4F
#define L_BREQ10N    0x50
#define L_CLK        0x51
#define L_CLKN       0x52
#define L_FASTN      0x53
#define L_BGNT3N     0x58
#define L_BGNT4N     0x59
#define L_BGNT5N     0x5A
#define L_BGNT6N     0x5B
#define L_BGNT7N     0x5C
#define L_BGNT8N     0x5D
#define L_BGNT9N     0x5E
#define L_BGNT10N    0x5F
#define L_RSRC0      0x80
#define L_RSRC1      0x81
#define L_RSRC2      0x82
#define L_RSRC3      0x83
#define L_RDST0      0x84
#define L_RDST1      0x85
#define L_RDST2      0x86
#define L_MEM0       0x87
#define L_MEM1       0x88
#define L_MEM2       0x89
#define L_SIZE0      0x8A
#define L_SIZE1      0x8B
#define L_SIGNED     0x8C
#define L_SUPER      0x8D
#define L_TLBW       0x8E
#define L_CRSEL0     0x8F
#define L_CRSEL1     0x90
#define L_CRSEL2     0x91
#define L_WSTBN      0x92
#define L_LOCKREQ    0x93
#define L_PCINC      0x94
#define L_MARPC      0x95
#define L_MARINC     0x96
#define L_CCLK       0x97
#define L_PWROK      0x98
#define L_LOCKN      0x99
#define L_IRQ0N      0x9A
#define L_IRQ1N      0x9B
#define L_IRQ2N      0x9C
#define L_IRQ3N      0x9D
#define L_IRQ4N      0x9E
#define L_IRQ5N      0x9F
#define L_IRQ6N      0xA0
#define L_IRQ7N      0xA1
#define L_BREQN      0xA2
#define L_BGNTN      0xA3
#define L_ACKN       0xA4
#define L_WAITN      0xA5
#define L_FAULT      0xA6
#define L_FC0        0xA7
#define L_FC1        0xA8
#define L_FC2        0xA9
#define L_FC3        0xAA
#define L_STALL      0xAB
#define L_CPUINN     0xAC
#define L_MMUINN     0xAD

#endif
