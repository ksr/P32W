/* (see pico/stdlib.h) */
#ifndef STUB_HARDWARE_GPIO_H
#define STUB_HARDWARE_GPIO_H
#include "pico/stdlib.h"
#define GPIO_OUT 1
#define GPIO_IN 0
void gpio_init_mask(uint gpio_mask);
void gpio_put(uint gpio, bool value);
void gpio_set_dir(uint gpio, bool out);
void gpio_set_dir_out_masked(uint32_t mask);
void gpio_set_dir_in_masked(uint32_t mask);
void gpio_put_masked(uint32_t mask, uint32_t value);
uint32_t gpio_get_all(void);
void gpio_disable_pulls(uint gpio);
#endif
