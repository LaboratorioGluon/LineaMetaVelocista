#include <string.h>
#include <esp_timer.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"

#include "sdkconfig.h" // generated by "make menuconfig"
#include "ssd1366.h"

#include <esp_adc/adc_continuous.h>
#include "adc.h"
#include "buttons.h"

static const char *TAG = "MAIN";

float trackDistance = 5476.192f;

adc_channel_t channels[1] = {ADC_CHANNEL_3};
adc_continuous_handle_t adcHandle;

uint32_t bestTime = 10000;
uint32_t timeTable[10] = {0};
uint32_t timeTableIndex = 0;
uint32_t timeTableOverflow = 0;

TaskHandle_t adcTask;
TaskHandle_t buttonTask;
QueueHandle_t adcDataQueue;

#define ADC_THRESHOLD 3000

typedef enum ADC_MESSAGE
{
    ADC_TRIGGERED = 0, // When signal crosses the threshold.
    ADC_FALLING
} ADC_MESSAGE;

typedef enum ADC_STATE
{
    ADC_WAITING = 0, // Waiting for shadow.
    ADC_ARMED,       // First shadow detected
    ADC_FALLEN,      // Adc is bright again
} ADC_STATE;

typedef enum SCREEN
{
    SCREEN_CURRENT = 0, // Time and speed of last lap
    SCREEN_LIST,        // Last time table
    SCREEN_BEST,        // Show best time
    SCREEN_STATS,       // Show statistics
} SCREEN;

SCREEN screenStatus = SCREEN_CURRENT;
uint64_t message;
uint32_t deltaTime = 0;

void updateScreen()
{
    char timeStr[64];
    if (screenStatus == SCREEN_CURRENT)
    {
        sprintf(timeStr, "Last Lap");
        task_ssd1306_display_text(timeStr, 1);

        if (deltaTime != 0)
        {
            sprintf(timeStr, "Time: %.3f  ", (float)deltaTime / 1000.0f);
            task_ssd1306_display_text(timeStr, 2);

            sprintf(timeStr, "Speed: %.3f  ", (float)trackDistance / deltaTime);
            task_ssd1306_display_text(timeStr, 4);
        }
        else
        {
            sprintf(timeStr, "Sin Tiempo");
            task_ssd1306_display_text(timeStr, 2);
        }
    }
    else if (screenStatus == SCREEN_BEST)
    {
        sprintf(timeStr, "Best Lap");
        task_ssd1306_display_text(timeStr, 1);

        sprintf(timeStr, "Time: %.3f  ", (float)bestTime / 1000.0f);
        task_ssd1306_display_text(timeStr, 2);

        sprintf(timeStr, "Speed: %.3f  ", (float)trackDistance / bestTime);
        task_ssd1306_display_text(timeStr, 4);
    }
    else if (screenStatus == SCREEN_LIST)
    {
        sprintf(timeStr, "List Laps");
        task_ssd1306_display_text(timeStr, 1);

        if (timeTableOverflow == 0 && timeTableIndex == 0)
        {
            char str[64] = " ";
            for (uint32_t i = 0; i < 5; i++)
            {
                task_ssd1306_display_text(str, i + 2);
            }
        }

        printf("Printing list\n");
        uint32_t tableI;
        uint32_t numLaps = (timeTableOverflow == 1 || timeTableIndex > 5) ? 5 : timeTableIndex;
        tableI = (timeTableIndex == 0 && timeTableOverflow == 1) ? 10 : timeTableIndex - 1;

        for (uint32_t i = 0; i < numLaps; i++)
        {
            printf("Fila %ld\n", i + 2);
            sprintf(timeStr, "%.3f | %.3f", (float)timeTable[tableI] / 1000.0f, (float)trackDistance / timeTable[tableI]);
            task_ssd1306_display_text(timeStr, i + 2);

            if (tableI == 0 && timeTableOverflow == 1)
            {
                tableI = 10;
            }
            tableI--;
        }
        char str[64] = "               ";
        for (uint32_t i = numLaps; i < 5; i++)
        {
            task_ssd1306_display_text(str, i + 2);
        }
    }
}

void taskButtonFunction()
{

    uint8_t b1, b2;
    while (1)
    {
        do
        {
            buttons_getStatus(&b1, &b2);
            vTaskDelay(1);
        } while (b1 != 0 && b2 != 0);

        if (b1 == 0)
        {
            printf("Switching screen from %d\n", screenStatus);
            switch (screenStatus)
            {
            case SCREEN_CURRENT:
                screenStatus = SCREEN_BEST;
                break;
            case SCREEN_BEST:
                screenStatus = SCREEN_LIST;
                break;
            case SCREEN_LIST:
                screenStatus = SCREEN_CURRENT;
                break;
            }

            task_ssd1306_display_clear(NULL);
            task_ssd1306_display_text((void *)"GluonGP Timer", 0);
            updateScreen();
        }
        else
        {
            deltaTime = 0;
            timeTableIndex = 0;
            timeTableOverflow = 0;
        }

        do
        {
            buttons_getStatus(&b1, &b2);
            vTaskDelay(1);
        } while (b1 != 1 && b2 != 1);
        vTaskDelay(10);
    }
}

void taskAdcFunction()
{
    uint8_t result[20 * SOC_ADC_DIGI_DATA_BYTES_PER_CONV];
    uint32_t resultLen = 0;
    ADC_MESSAGE toSendMessage;
    uint32_t singleData = 0;
    uint64_t proposedTime = 0;
    ADC_STATE state = ADC_WAITING;

    while (1)
    {
        adc_continuous_read(adcHandle, result, 20 * SOC_ADC_DIGI_DATA_BYTES_PER_CONV, &resultLen, 3000);
        for (int i = 0; i < resultLen; i += SOC_ADC_DIGI_RESULT_BYTES)
        {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result[i];
            // p->type2.data;
            singleData = p->type2.data;

            if (state == ADC_WAITING)
            {
                if (singleData > ADC_THRESHOLD)
                {
                    state = ADC_ARMED;
                }
            }
            else if (state == ADC_ARMED)
            {
                if (singleData < ADC_THRESHOLD)
                {
                    proposedTime = esp_timer_get_time();
                    state = ADC_FALLEN;
                }
            }
            else if (state == ADC_FALLEN)
            {
                if (singleData > ADC_THRESHOLD)
                {
                    state = ADC_ARMED;
                }
            }
        }
        if (state == ADC_FALLEN && esp_timer_get_time() - proposedTime > 100000)
        {
            xQueueSend(adcDataQueue, (void *)&proposedTime, portMAX_DELAY);
            state = ADC_WAITING;
        }

        vTaskDelay(2);
    }
}

void app_main(void)
{

    // Freertos things
    adcDataQueue = xQueueCreate(1, sizeof(uint64_t));

    continuous_adc_init(channels, 1, &adcHandle);

    // adc_continuous_read(adcHandle, result, 20*SOC_ADC_DIGI_DATA_BYTES_PER_CONV, &resultLen, 3000);

    ESP_LOGE(TAG, "Hola mundo!");
    i2c_master_init();
    ssd1306_init();
    buttons_init(GPIO_NUM_9, GPIO_NUM_10);

    task_ssd1306_display_clear(NULL);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    task_ssd1306_display_clear(NULL);
    task_ssd1306_display_text((void *)"GluonGP Timer", 0);

    char timeStr[64];

    xTaskCreate(taskAdcFunction, "Adc_Task", 4096, NULL, 9, &adcTask);
    xTaskCreate(taskButtonFunction, "Button_taskl", 4096, NULL, 8, &buttonTask);

    uint32_t receivedMessage;

    uint64_t lastTimeStamp = 0;

    updateScreen();
    while (1)
    {

        xQueueReceive(adcDataQueue, &receivedMessage, portMAX_DELAY);
        // printf("Desde main!\n");
        if (lastTimeStamp != 0)
        {
            deltaTime = (uint32_t)(receivedMessage - lastTimeStamp) / 1000;

            // Store the time
            timeTable[timeTableIndex] = deltaTime;
            timeTableIndex++;

            if (timeTableIndex > 10)
            {
                timeTableOverflow = 1;
                timeTableIndex = 0;
            }

            // Update best time

            if (deltaTime < bestTime)
            {
                bestTime = deltaTime;
            }

            updateScreen();
        }
        lastTimeStamp = receivedMessage;

        // task_ssd1306_display_text(timeStr, 3);
        // vTaskDelay(pdMS_TO_TICKS(300));
    }
}