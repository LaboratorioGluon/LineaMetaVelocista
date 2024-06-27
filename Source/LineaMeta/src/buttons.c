#include "buttons.h"

static gpio_num_t gpiob1, gpiob2;

void buttons_init(gpio_num_t b1_gpio, gpio_num_t b2_gpio)
{
    
    gpiob1 = b1_gpio;
    gpiob2 = b2_gpio;
    gpio_config_t config;
    config.intr_type = GPIO_INTR_DISABLE;
    config.mode = GPIO_MODE_INPUT;
    config.pin_bit_mask = (1U << gpiob1) | (1U << gpiob2);
    config.pull_down_en = GPIO_PULLUP_DISABLE;
    config.pull_up_en = GPIO_PULLUP_ENABLE;

    gpio_config(&config);
}


void buttons_getStatus(uint8_t * b1, uint8_t * b2)
{
    *b1 = gpio_get_level(gpiob1);
    *b2 = gpio_get_level(gpiob2);
}