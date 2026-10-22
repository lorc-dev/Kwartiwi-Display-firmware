#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"

#define DATA_MANAGER_MAX_DEMAND_YEAR_ITEMS 13

#define DATA_MANAGER_MAX_DEMAND_SHORT_TERM_ITEMS (60 * 15)  // 1 second * 15 minutes

ESP_EVENT_DECLARE_BASE(DATA_MANAGER_EVENTS);

typedef enum {
    DATA_MANAGER_NEW_METER_DATA_AVAILABLE,
    DATA_MANAGER_NEW_METER_HISTORY_DATA_AVAILABLE,
    DATA_MANAGER_NEW_USAGE_DATA_AVAILABLE,
} data_manager_event_id_t;


enum data_manager_data_fields_e {
    DM_DF_P1_TIMESTAMP,
    DM_DF_ELECTRICITY_DELIVERED_TARIFF1,
    DM_DF_ELECTRICITY_DELIVERED_TARIFF2,
    DM_DF_ELECTRICITY_RETURNED_TARIFF1,
    DM_DF_ELECTRICITY_RETURNED_TARIFF2,
    DM_DF_CURRENT_AVG_DEMAND,
    DM_DF_CURRENT_POWER_USAGE,
    DM_DF_CURRENT_POWER_RETURN,
    DM_DF_ELECTRICITY_ACTIVE_TARIFF,
    DM_DF_MAX_DEMAND_MONTH,
    DM_DF_PREDICTED_PEAK,
    DM_DF_MAX_DEMAND_YEAR,
    DM_DF_MAX_DEMAND_SHORT_TERM,
    DM_DF_ELECTRICITY_USAGE_TODAY,
    DM_DF_WATER_USAGE_TODAY,
    DM_DF_GAS_USAGE_TODAY,
    DM_DF_ELECTRICITY_COST_TODAY,
    DM_DF_WATER_COST_TODAY,
    DM_DF_GAS_COST_TODAY,
    DM_DF_LENGTH,
};

typedef struct {
    time_t timestamp;
    float demand;
} data_manager_demand_data_point_t;

typedef struct {
    time_t p1_timestamp;
    float electricity_delivered_tariff1;
    float electricity_delivered_tariff2;
    float electricity_returned_tariff1;
    float electricity_returned_tariff2;
    float current_avg_demand;
    float current_power_usage;
    float current_power_return;
    uint8_t electricity_active_tariff;
    data_manager_demand_data_point_t max_demand_active_month;
    data_manager_demand_data_point_t predicted_peak;
    // Usage totals for today. A negative value means no value has been received yet.
    float electricity_usage_today_kwh;
    float water_usage_today_l;
    float gas_usage_today_m3;
    // Cost totals for today, in euro.
    float electricity_cost_today_eur;
    float water_cost_today_eur;
    float gas_cost_today_eur;
} data_manager_meter_data_t;

typedef struct {
    data_manager_demand_data_point_t max_demand_year[DATA_MANAGER_MAX_DEMAND_YEAR_ITEMS];
    uint16_t max_demand_year_items;
    data_manager_demand_data_point_t max_demand_short_term[DATA_MANAGER_MAX_DEMAND_SHORT_TERM_ITEMS];
    uint16_t max_demand_short_term_items;
} data_manager_history_data_t;

typedef struct {
    data_manager_meter_data_t meter_data;
    data_manager_history_data_t history_data;
} data_manager_data_t;

// Function prototypes
void data_manager_init(void);
SemaphoreHandle_t data_manager_get_data_mutex_handle(void);
data_manager_meter_data_t * data_manager_get_meter_data(void);
data_manager_history_data_t * data_manager_get_history_data(void);
void data_manager_set_field(enum data_manager_data_fields_e field, void * value);
void data_manager_get_field(enum data_manager_data_fields_e field, void * value);
void data_manager_add_max_demand_short_term_history_item(float value, time_t timestamp);
uint16_t data_manager_get_short_term_max_demand_history(data_manager_demand_data_point_t items[], uint16_t max_items);
void data_manager_notify_new_meter_data_available(void);
void data_manager_notify_new_meter_history_data_available(void);
void data_manager_notify_new_usage_data_available(void);


#endif //DATA_MANAGER_H