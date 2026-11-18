/* (see pico/stdlib.h) */
#ifndef STUB_HARDWARE_ADC_H
#define STUB_HARDWARE_ADC_H
#include "pico/stdlib.h"
void adc_init(void);
void adc_gpio_init(uint gpio);
void adc_select_input(uint input);
uint16_t adc_read(void);
#endif
