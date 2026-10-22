/**
 * @file data_manager.c
 * @brief Data manager
 *
 * The data manager is responsible for managing the data used by the application.
 * The data can be set by different data providers (e.g. the MQTT client) and can be used by the rest of the application.
 */

#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "data_manager.h"

ESP_EVENT_DEFINE_BASE(DATA_MANAGER_EVENTS);

extern esp_event_loop_handle_t app_loop_handle;

static const char *TAG = "data_manager";
data_manager_data_t data_manager_data;
SemaphoreHandle_t data_manager_data_mutex;

/**
 * @brief Initialize the data manager
 */
void data_manager_init(void) {
    ESP_LOGI(TAG, "Initializing data manager");
    data_manager_data_mutex = xSemaphoreCreateMutex();
    if (data_manager_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create data manager data mutex");
        abort();
    }
}

/**
 * @brief Get the data manager data mutex handle
 *
 * @return The mutex handle
 */
SemaphoreHandle_t data_manager_get_data_mutex_handle(void) {
    return data_manager_data_mutex;
}

/**
 * @brief Get a pointer to the meter data
 *
 * @note Locks the data manager data mutex before using the data
 *
 * @return The meter data pointer
 */
data_manager_meter_data_t * data_manager_get_meter_data(void) {
    return &data_manager_data.meter_data;
}

/**
 * @brief Get a pointer to the history data
 *
 * @note Locks the data manager data mutex before using the data
 *
 * @return The history data pointer
 */
data_manager_history_data_t * data_manager_get_history_data(void) {
    return &data_manager_data.history_data;
}

void data_manager_notify_new_meter_data_available(void) {
    ESP_ERROR_CHECK(esp_event_post_to(app_loop_handle, DATA_MANAGER_EVENTS, DATA_MANAGER_NEW_METER_DATA_AVAILABLE, NULL, 0, portMAX_DELAY));
}

void data_manager_notify_new_meter_history_data_available(void) {
    ESP_ERROR_CHECK(esp_event_post_to(app_loop_handle, DATA_MANAGER_EVENTS, DATA_MANAGER_NEW_METER_HISTORY_DATA_AVAILABLE, NULL, 0, portMAX_DELAY));
}

/**
 * @brief Add a item to the max demand short term history
 *
 * The max demand short term history is a ring buffer of the last 15 minutes of max demand data.
 * The oldest item is overwritten when the buffer is full.
 *
 * @param[in] value The maximum demand at the given timestamp
 * @param[in] timestamp The timestamp of the maximum demand
 */
void data_manager_add_max_demand_short_term_history_item(float value, time_t timestamp) {
    xSemaphoreTake(data_manager_data_mutex, portMAX_DELAY);

    // Add the item to the history
    data_manager_data.history_data.max_demand_short_term[data_manager_data.history_data.max_demand_short_term_items].demand = value;
    data_manager_data.history_data.max_demand_short_term[data_manager_data.history_data.max_demand_short_term_items].timestamp = timestamp;

    // Increment the index
    data_manager_data.history_data.max_demand_short_term_items++;
    if (data_manager_data.history_data.max_demand_short_term_items >= DATA_MANAGER_MAX_DEMAND_SHORT_TERM_ITEMS) {
        data_manager_data.history_data.max_demand_short_term_items = 0;
    }

    xSemaphoreGive(data_manager_data_mutex);

}

/**
 * @brief Get the maximum demand short term history
 *
 * The number of items returned is the minimum of the number of items in the history and the max_items parameter.
 * The items are returned in chronological order, with the oldest item first.
 *
 * @param[out] items The array to store the items in, must be at least max_items in size
 * @param[in] max_items The maximum number of items to return
 * @return The number of items returned
 */
uint16_t data_manager_get_short_term_max_demand_history(data_manager_demand_data_point_t items[], uint16_t max_items) {
    // Limit the number of items to the maximum
    if (max_items > DATA_MANAGER_MAX_DEMAND_SHORT_TERM_ITEMS) {
        max_items = DATA_MANAGER_MAX_DEMAND_SHORT_TERM_ITEMS;
    }

    // Lock the data manager data mutex
    xSemaphoreTake(data_manager_data_mutex, portMAX_DELAY);

    // Find the first index of the first item and the number of items in the history
    uint16_t first_index = (data_manager_data.history_data.max_demand_short_term_items - max_items) % DATA_MANAGER_MAX_DEMAND_SHORT_TERM_ITEMS;
    uint16_t num_items = max_items;
    while (data_manager_data.history_data.max_demand_short_term[first_index].timestamp == 0) {
        first_index++;
        if (first_index >= DATA_MANAGER_MAX_DEMAND_SHORT_TERM_ITEMS) {
            first_index = 0;
        }
        num_items--;
        if (num_items == 0) {
            break;
        }
    }

    // Copy the items
    for (uint16_t i = 0; i < num_items; i++) {
        items[i].demand = data_manager_data.history_data.max_demand_short_term[first_index].demand;
        items[i].timestamp = data_manager_data.history_data.max_demand_short_term[first_index].timestamp;
        first_index++;
        if (first_index >= DATA_MANAGER_MAX_DEMAND_SHORT_TERM_ITEMS) {
            first_index = 0;
        }
    }

    // Unlock the data manager data mutex
    xSemaphoreGive(data_manager_data_mutex);

    return num_items;
}

/**
 * @brief Set a field in the data manager data
 *
 * @note To set multiple fields, use the data structure directly using data_manager_get_data() and data_manager_get_data_mutex_handle()
 * @note The array for the MAX_DEMAND_YEAR should be DATA_MANAGER_MAX_DEMAND_YEAR_ITEMS in size, or the timestamp of the last item should be 0
 *
 * @param[in] field The field to set
 * @param[in] value A pointer to the value to set
 */
void data_manager_set_field(enum data_manager_data_fields_e field, void * value) {

    xSemaphoreTake(data_manager_data_mutex, portMAX_DELAY);
    switch (field) {
        case DM_DF_P1_TIMESTAMP:
            data_manager_data.meter_data.p1_timestamp = *(time_t *)value;
            break;
        case DM_DF_ELECTRICITY_DELIVERED_TARIFF1:
            data_manager_data.meter_data.electricity_delivered_tariff1 = *(float *)value;
            break;
        case DM_DF_ELECTRICITY_DELIVERED_TARIFF2:
            data_manager_data.meter_data.electricity_delivered_tariff2 = *(float *)value;
            break;
        case DM_DF_ELECTRICITY_RETURNED_TARIFF1:
            data_manager_data.meter_data.electricity_returned_tariff1 = *(float *)value;
            break;
        case DM_DF_ELECTRICITY_RETURNED_TARIFF2:
            data_manager_data.meter_data.electricity_returned_tariff2 = *(float *)value;
            break;
        case DM_DF_CURRENT_AVG_DEMAND:
            data_manager_data.meter_data.current_avg_demand = *(float *)value;
            break;
        case DM_DF_CURRENT_POWER_USAGE:
            data_manager_data.meter_data.current_power_usage = *(float *)value;
            break;
        case DM_DF_CURRENT_POWER_RETURN:
            data_manager_data.meter_data.current_power_return = *(float *)value;
            break;
        case DM_DF_ELECTRICITY_ACTIVE_TARIFF:
            data_manager_data.meter_data.electricity_active_tariff = *(uint8_t *)value;
            break;
        case DM_DF_MAX_DEMAND_MONTH:
            data_manager_data.meter_data.max_demand_active_month = *(data_manager_demand_data_point_t *)value;
            break;
        case DM_DF_PREDICTED_PEAK:
            data_manager_data.meter_data.predicted_peak = *(data_manager_demand_data_point_t *)value;
            break;
        case DM_DF_MAX_DEMAND_YEAR:
            for (uint8_t i = 0; i < DATA_MANAGER_MAX_DEMAND_YEAR_ITEMS; i++) {
                if (((data_manager_demand_data_point_t *)value)[i].timestamp == 0) {
                    data_manager_data.history_data.max_demand_year_items = i;
                    break;
                }
                data_manager_data.history_data.max_demand_year[i] = ((data_manager_demand_data_point_t *)value)[i];

            }
            break;
        case DM_DF_MAX_DEMAND_SHORT_TERM:
            // TODO: Implement setting of short term max demand
            break;
        case DM_DF_LENGTH:
            ESP_LOGE(TAG, "Invalid data manager data field");
            break;
    }

    xSemaphoreGive(data_manager_data_mutex);
}

/**
 * @brief Get a field from the data manager data
 *
 * @note To get multiple fields, use the data structure directly using data_manager_get_data() and data_manager_get_data_mutex_handle()
 * @note The array for the MAX_DEMAND_YEAR should be at least DATA_MANAGER_MAX_DEMAND_YEAR_ITEMS in size
 *
 * @param[in] field The field to get
 * @param[out] value Where to store the value
 */
void data_manager_get_field(enum data_manager_data_fields_e field, void * value) {
    xSemaphoreTake(data_manager_data_mutex, portMAX_DELAY);
    switch (field) {
        case DM_DF_P1_TIMESTAMP:
            *(time_t *) value = data_manager_data.meter_data.p1_timestamp;
            break;
        case DM_DF_ELECTRICITY_DELIVERED_TARIFF1:
            *(float *) value = data_manager_data.meter_data.electricity_delivered_tariff1;
            break;
        case DM_DF_ELECTRICITY_DELIVERED_TARIFF2:
            *(float *) value = data_manager_data.meter_data.electricity_delivered_tariff2;
            break;
        case DM_DF_ELECTRICITY_RETURNED_TARIFF1:
            *(float *) value = data_manager_data.meter_data.electricity_returned_tariff1;
            break;
        case DM_DF_ELECTRICITY_RETURNED_TARIFF2:
            *(float *) value = data_manager_data.meter_data.electricity_returned_tariff2;
            break;
        case DM_DF_CURRENT_AVG_DEMAND:
            *(float *) value = data_manager_data.meter_data.current_avg_demand;
            break;
        case DM_DF_CURRENT_POWER_USAGE:
            *(float *) value = data_manager_data.meter_data.current_power_usage;
            break;
        case DM_DF_CURRENT_POWER_RETURN:
            *(float *) value = data_manager_data.meter_data.current_power_return;
            break;
        case DM_DF_ELECTRICITY_ACTIVE_TARIFF:
            *(uint8_t *) value = data_manager_data.meter_data.electricity_active_tariff;
            break;
        case DM_DF_MAX_DEMAND_MONTH:
            *(data_manager_demand_data_point_t *) value = data_manager_data.meter_data.max_demand_active_month;
            break;
        case DM_DF_MAX_DEMAND_YEAR:
            for (int i = 0; i < DATA_MANAGER_MAX_DEMAND_YEAR_ITEMS; i++) {
                ((data_manager_demand_data_point_t *) value)[i] = data_manager_data.history_data.max_demand_year[i];
            }
            break;
        case DM_DF_PREDICTED_PEAK:
            *(data_manager_demand_data_point_t *) value = data_manager_data.meter_data.predicted_peak;
            break;
        case DM_DF_MAX_DEMAND_SHORT_TERM:
            // TODO: Implement getting of short term max demand
            break;
        case DM_DF_LENGTH:
            ESP_LOGE(TAG, "Invalid data manager data field");
            break;
    }

    xSemaphoreGive(data_manager_data_mutex);
}
