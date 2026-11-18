/* pico.c -- the debug card's firmware on its Raspberry Pi Pico 2 (A1): the local bus on GP0-18, the host on
 * USB (docs/debug-card-theory.md section 3; D135). The logic is core.c; this is hal.h for the Pico.
 *
 *   GP0-7    PD0-7: data, through the 74LVC245 to the card's XD0-7
 *   GP8-14   XA0-6: the register address
 *   GP15     -XWR (10k up to 3.3 V on the card)      GP16  -XRD (10k up)
 *   GP17     XDIR: high, the Pico drives XD; low, XD drives the Pico (the 245 is always enabled)
 *   GP18     CRESN: low holds the card's control logic clear (10k down on the card)
 *
 *   GP26     ADC0: the card's 5 V, halved (R8, R9: D136)
 *
 * From reset the pins are inputs and the card's resistors hold the strobes off and CRESN low. The firmware
 * first drives CRESN low itself (RP2350-E9: on an A2-stepping chip a pad's input leakage can lift a pin that
 * only a weak pull-down holds; the card's is 4.7k since D136, 10k before: D135), then XDIR low. Pins driven
 * low cannot feed a card with no power; the address and strobes are driven only once the card's 5 V is up and
 * it has answered (core.c, powered() and probe()).
 *
 * Build: the Raspberry Pi Pico SDK (2.x), PICO_SDK_PATH set; hardware/debug-card/firmware/README.md.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hal.h"
#include "core.h"

#define M_DATA  0x000000FFu
#define M_ADDR  0x00007F00u
#define P_XWRN  15
#define P_XRDN  16
#define P_XDIR  17
#define P_CRESN 18
#define M_ALL   0x0007FFFFu
#define P_V5    26                      /* ADC0 */

/* the local bus's times (ns): the card's oscillator period is 41.7 ns (hal.h) */
#define T_SETUP  30                     /* XA and XD steady before the strobe (TIL311: 50 ns before its end) */
#define T_STROBE 70                     /* -XWR low: more than an oscillator period, under two 12 MHz samples */
#define T_HOLD   50                     /* XD after -XWR rises (TIL311: 40 ns) */
#define T_ACCESS 200                    /* -XRD low to XD read: the decoders, a 74HCT541, the 74LVC245 */
#define T_GAP    30                     /* between operations */

static uint32_t cycles_per_us;

void hal_delay_ns(uint32_t ns)
{
    busy_wait_at_least_cycles((ns * cycles_per_us + 999) / 1000);
}

uint32_t hal_us(void) { return time_us_32(); }

void hal_cresn(int level)
{
    gpio_put(P_CRESN, level != 0);
    gpio_set_dir(P_CRESN, GPIO_OUT);
}

void hal_release(void)
{
    gpio_set_dir_in_masked(M_DATA | M_ADDR | 1u << P_XWRN | 1u << P_XRDN);
    gpio_put(P_XDIR, 0);
    gpio_set_dir(P_XDIR, GPIO_OUT);
    hal_cresn(0);
}

void hal_take(void)
{
    gpio_put_masked(M_ADDR | 1u << P_XWRN | 1u << P_XRDN, 1u << P_XWRN | 1u << P_XRDN);
    gpio_set_dir_out_masked(M_ADDR | 1u << P_XWRN | 1u << P_XRDN);
}

void hal_wr(uint8_t xa, uint8_t xd)
{
    gpio_put_masked(M_ADDR | M_DATA, (uint32_t)xa << 8 | xd);
    gpio_put(P_XDIR, 1);                /* (the 245 first lets go of the Pico's side, then the Pico drives it) */
    gpio_set_dir_out_masked(M_DATA);
    hal_delay_ns(T_SETUP);
    gpio_put(P_XWRN, 0);
    hal_delay_ns(T_STROBE);
    gpio_put(P_XWRN, 1);
    hal_delay_ns(T_HOLD);
    gpio_set_dir_in_masked(M_DATA);
    gpio_put(P_XDIR, 0);
    hal_delay_ns(T_GAP);
}

uint8_t hal_rd(uint8_t xa)
{
    gpio_put_masked(M_ADDR, (uint32_t)xa << 8);
    gpio_put(P_XRDN, 0);
    hal_delay_ns(T_ACCESS);
    uint8_t v = (uint8_t)(gpio_get_all() & M_DATA);
    gpio_put(P_XRDN, 1);
    hal_delay_ns(T_GAP);
    return v;
}

void hal_put(const char *s)
{
    fputs(s, stdout);
}

/* the card's 5 V: GP26 (ADC0) sees it halved; 12 bits of the ADC's 3.3 V, sixteen readings averaged */
int hal_vcc_mv(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++)
        sum += adc_read();
    return (int)(sum * 2u * 3300u / (16u * 4096u));
}

int main(void)
{
    gpio_init_mask(M_ALL);              /* (SIO, inputs, outputs low) */
    for (uint p = 0; p <= P_CRESN; p++)
        gpio_disable_pulls(p);          /* the card's resistors are the pulls; none of the RP2350's (E9) */
    hal_cresn(0);                       /* first: the card's control logic held clear */
    adc_init();
    adc_gpio_init(P_V5);                /* (its digital input off: an analog pin) */
    adc_select_input(0);
    cycles_per_us = clock_get_hz(clk_sys) / 1000000;
    stdio_init_all();
    dbg_init();
    static char line[1024];
    int n = 0;
    for (;;) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT)
            continue;
        if (c == '\n' || c == '\r') {
            line[n] = 0;
            if (n)
                dbg_line(line);
            n = 0;
            fflush(stdout);
        } else if (n < (int)sizeof line - 1)
            line[n++] = (char)c;
    }
}
