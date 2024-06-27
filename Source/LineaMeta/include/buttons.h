#ifndef __BUTTONS_H__
#define __BUTTONS_H__

#include <stdint.h>

#include <driver/gpio.h>

void buttons_init(gpio_num_t b1_gpio, gpio_num_t b2_gpio);
void buttons_getStatus(uint8_t * b1, uint8_t * b2);

#endif