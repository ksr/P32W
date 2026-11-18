/* hal.h -- what the debug card's firmware needs of the board it runs on (docs/debug-card-theory.md section 3).
 *
 * On the card, pico.c gives it: the Pico 2's GP0-18, its USB serial port and its timer. On the Mac, host.c
 * gives it, and generators/sim_dbgfw.py turns each local-bus operation into the card's gate-level netlist's.
 */
#ifndef DBG_HAL_H
#define DBG_HAL_H
#include <stdint.h>

/* One local-bus write: XA6-0 = xa, XD = xd, -XWR low and back. Registers take XD at -XWR's rise; GO, ARM and
 * STEP are preset while it is low, so the strobe is held at least one of the card's oscillator periods (STEP's
 * preset must span an oscillator edge) and well under eight (a STEP held past the next phase 3 gives two
 * periods). pico.c: 70 ns. */
void hal_wr(uint8_t xa, uint8_t xd);
/* One local-bus read: XA6-0 = xa, -XRD low, XD sampled once the card's buffers have it, -XRD back. */
uint8_t hal_rd(uint8_t xa);
/* CRESN: 0 holds the card's control logic clear (its control registers, GO, ARM, STEP, the DMA and capture
 * state machines, the interrupt register); 1 lets it go. */
void hal_cresn(int level);
/* Let go of the local bus: XA, -XWR, -XRD and the data inputs, the card's pull-ups holding the strobes off;
 * XDIR (the 74LVC245's, on the Pico's own 3.3 V) low; CRESN driven low. For a card that may have no power. */
void hal_release(void);
/* Take the local bus again: XA outputs, -XWR and -XRD driven high. */
void hal_take(void);
/* At least this many nanoseconds. */
void hal_delay_ns(uint32_t ns);
/* A free-running microsecond count, for time-outs (it may wrap). */
uint32_t hal_us(void);
/* Text to the host, as it is (the firmware ends its lines with \n). */
void hal_put(const char *s);

#endif
