#ifndef __ADC_H__
#define __ADC_H__

#include <esp_log.h>
#include <esp_adc/adc_continuous.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>

void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle);


#endif