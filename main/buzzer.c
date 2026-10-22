/**
 * @file buzzer.c
 * @brief This file contains the buzzer task and related functions
 */

#include <sys/cdefs.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "buzzer.h"

#define BUZZER_QUEUE_LENGTH 10

static const char *TAG = "buzzer";
static QueueHandle_t buzzer_queue;

// Function prototypes
static void buzzer_init(void);
static void beep_long(void);
static void beep_short(void);
static void beep_double(void);


/**
 * @brief Initialize the buzzer, and process buzzer events from the buzzer queue
 *
 * @param pvParameters unused
 */
_Noreturn void buzzer_task(void *pvParameters) {
    enum buzzer_beep_type_e beep_type;
    buzzer_queue = xQueueCreate(BUZZER_QUEUE_LENGTH, sizeof(enum buzzer_beep_type_e));

    // Initialize the buzzer hardware
    buzzer_init();

    // Process buzzer events from the buzzer queue
    while (true) {
        if (xQueueReceive(buzzer_queue, &beep_type, portMAX_DELAY)) {
            switch (beep_type) {
                case BUZZER_BEEP_TYPE_SHORT:
                    beep_short();
                    break;
                case BUZZER_BEEP_TYPE_LONG:
                    beep_long();
                    break;
                case BUZZER_BEEP_TYPE_DOUBLE:
                    beep_double();
                    break;
                default:
                    ESP_LOGE(TAG, "Unknown beep type");
            }
        }
    }
}

/**
 * @brief Beep the buzzer based on the beep type
 *
 * Add a beep to the buzzer queue.
 * This function is thread-safe, and can be called from any task.
 *
 * @param beep_type The type of beep to play
 */
void buzzer_beep(enum buzzer_beep_type_e beep_type) {
    xQueueSend(buzzer_queue, &beep_type, portMAX_DELAY);
}

/**
 * @brief Initialize the buzzer hardware
 *
 * Uses the LEDC peripheral to generate a PWM signal on the buzzer pin.
 * By default, the buzzer is off (duty = 0).
 *
 * @note This function should only be called once, from the buzzer task
 */
static void buzzer_init(void) {
    ESP_LOGI(TAG, "Initializing buzzer");
    ledc_timer_config_t ledc_timer = {
            .duty_resolution = BUZZER_RESOLUTION,
            .freq_hz = BUZZER_DEFAULT_FREQ_HZ,
            .speed_mode = BUZZER_LEDC_SPEED_MODE,
            .timer_num = BUZZER_LEDC_TIMER,
            .clk_cfg = LEDC_AUTO_CLK,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
            .gpio_num = BUZZER_PIN_NUM,
            .speed_mode = BUZZER_LEDC_SPEED_MODE,
            .channel = BUZZER_LEDC_CHANNEL,
            /* intr_type is deprecated in ESP-IDF v6, the driver handles the interrupt */
            .timer_sel = BUZZER_LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

/**
 * @brief Beep with default frequency for BUZZER_BEEP_SHORT_TIME_MS
 * @warning This function is not thread safe, and should only be called from the buzzer task
 */
static void beep_short(void) {
    ESP_LOGD(TAG, "Beeping short");
    ledc_set_freq(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_TIMER, BUZZER_DEFAULT_FREQ_HZ);
    ledc_set_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_SHORT_TIME_MS));
    ledc_set_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL);
}

/**
 * @brief Beep with default frequency for BUZZER_BEEP_LONG_TIME_MS
 * @warning This function is not thread safe, and should only be called from the buzzer task
 */
static void beep_long(void) {
    ESP_LOGD(TAG, "Beeping long");
    ledc_set_freq(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_TIMER, BUZZER_DEFAULT_FREQ_HZ);
    ledc_set_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_LONG_TIME_MS));
    ledc_set_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL);
}

/**
 * @brief Beep with a lower freq, then a higher freq for BUZZER_BEEP_SHORT_TIME_MS and BUZZER_BEEP_LONG_TIME_MS
 * @warning This function is not thread safe, and should only be called from the buzzer task
 */
static void beep_double(void) {
    ESP_LOGD(TAG, "Beeping double");
    ledc_set_freq(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_TIMER, BUZZER_BEEP_DOUBLE_LOW_FREQ_HZ);
    ledc_set_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_SHORT_TIME_MS));
    ledc_set_freq(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_TIMER, BUZZER_BEEP_DOUBLE_HIGH_FREQ_HZ);
    vTaskDelay(pdMS_TO_TICKS(BUZZER_BEEP_LONG_TIME_MS));
    ledc_set_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_SPEED_MODE, BUZZER_LEDC_CHANNEL);
}