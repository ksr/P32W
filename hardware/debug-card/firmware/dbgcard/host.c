/* host.c -- the debug card's firmware on the Mac, for generators/sim_dbgfw.py (make test).
 *
 * The same core.c as the Pico runs; hal.h's functions are the simulator's, bound at run time: each local-bus
 * operation becomes the card's gate-level netlist's (sim_debug.py's Pico model), each wait so many of the
 * card's oscillator periods. dbg_host_line() runs one command and gives back its reply.
 *
 *     cc -O1 -shared -fPIC -o libdbgcard.so core.c host.c
 */
#include <stdlib.h>
#include <string.h>
#include "hal.h"
#include "core.h"

typedef void (*wr_fn)(uint8_t, uint8_t);
typedef uint8_t (*rd_fn)(uint8_t);
typedef void (*level_fn)(int);
typedef void (*void_fn)(void);
typedef void (*delay_fn)(uint32_t);
typedef uint32_t (*us_fn)(void);

static wr_fn f_wr;
static rd_fn f_rd;
static level_fn f_cresn;
static void_fn f_release, f_take;
static delay_fn f_delay;
static us_fn f_us;
static char *reply;
static size_t used, room;
static int vcc_mv = -1;                 /* (unknown, as before D136, until a test sets it) */

void dbg_host_bind(wr_fn wr, rd_fn rd, level_fn cresn, void_fn release, void_fn take, delay_fn delay, us_fn us)
{
    f_wr = wr; f_rd = rd; f_cresn = cresn; f_release = release; f_take = take; f_delay = delay; f_us = us;
}

void hal_wr(uint8_t xa, uint8_t xd) { f_wr(xa, xd); }
uint8_t hal_rd(uint8_t xa) { return f_rd(xa); }
void hal_cresn(int level) { f_cresn(level); }
void hal_release(void) { f_release(); }
void hal_take(void) { f_take(); }
void hal_delay_ns(uint32_t ns) { f_delay(ns); }
uint32_t hal_us(void) { return f_us(); }
int hal_vcc_mv(void) { return vcc_mv; }

/* the card's 5 V as the Pico's ADC would see it, in millivolts; -1: unknown */
void dbg_host_vcc(int mv) { vcc_mv = mv; }

void hal_put(const char *s)
{
    size_t n = strlen(s);
    if (used + n + 1 > room) {
        room = (used + n + 1) * 2;
        reply = realloc(reply, room);
    }
    memcpy(reply + used, s, n + 1);
    used += n;
}

static const char *take_reply(void)
{
    if (!reply)
        hal_put("");
    used = 0;
    return reply;
}

/* power-on: the firmware's start, its reply (none) discarded */
void dbg_host_init(void)
{
    dbg_init();
    take_reply();
}

/* one command line; the reply, valid until the next call */
const char *dbg_host_line(const char *line)
{
    char buf[1024];
    strncpy(buf, line, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    used = 0;
    if (reply)
        reply[0] = 0;
    dbg_line(buf);
    return take_reply();
}
