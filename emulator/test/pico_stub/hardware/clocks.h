/* (see pico/stdlib.h) */
#ifndef STUB_HARDWARE_CLOCKS_H
#define STUB_HARDWARE_CLOCKS_H
#include <stdint.h>
enum clock_index { clk_gpout0, clk_gpout1, clk_gpout2, clk_gpout3, clk_ref, clk_sys, clk_peri, clk_hstx, clk_usb, clk_adc };
typedef enum clock_index clock_handle_t;
uint32_t clock_get_hz(clock_handle_t clock);
#endif
