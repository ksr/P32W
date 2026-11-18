/* (the debug card's pico.c includes this: make test compiles it against these declarations, the Raspberry Pi
 * Pico SDK 2.x's for the calls it makes, to catch a slip before the SDK is at hand; nothing is linked) */
#ifndef STUB_PICO_STDLIB_H
#define STUB_PICO_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
typedef unsigned int uint;
enum { PICO_OK = 0, PICO_ERROR_TIMEOUT = -1 };
bool stdio_init_all(void);
int getchar_timeout_us(uint32_t timeout_us);
uint32_t time_us_32(void);
void busy_wait_at_least_cycles(uint32_t minimum_cycles);
#endif
