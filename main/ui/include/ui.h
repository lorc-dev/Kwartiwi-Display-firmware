#ifndef UI_H
#define UI_H

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_OK,
    UI_ERROR,
} ui_error_t;

typedef enum {
    UI_WIFI_AUTH_OPEN = 0,         /**< authenticate mode : open */
    UI_WIFI_AUTH_WPA2_PSK,         /**< authenticate mode : WPA2_PSK */
} ui_wifi_auth_mode_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    ui_wifi_auth_mode_t auth_mode;
} ui_wifi_network_t;

typedef struct {
    char hostname[256];
    uint32_t ipv4;
} ui_server_t;


#include "main_screen.h"
#include "loading_screen.h"
#include "setup_screen.h"

#define COLOR_KWARTIWI_RED lv_color_hex(0xEB440E)
#define COLOR_KWARTIWI_ORANGE lv_color_hex(0xEC9106)
#define COLOR_KWARTIWI_GREY lv_color_hex(0x4D4D4D)
#define COLOR_KWARTIWI_GREEN lv_color_hex(0x6AD011)
#define COLOR_KWARTIWI_BLUE lv_color_hex(0x623CEA)
#define COLOR_KWARTIWI_YELLOW lv_color_hex(0xFFD700)
#define COLOR_WHITE lv_color_hex(0xFFFFFF)

#define SIMULATOR 0

#if SIMULATOR

#include <stdio.h>

#define UI_LOGV(TAG, format, ...) printf("[V] " TAG ": " format "\n", ##__VA_ARGS__)
#define UI_LOGI(TAG, format, ...) printf("[I] " TAG ": " format "\n", ##__VA_ARGS__)
#define UI_LOGD(TAG, format, ...) printf("[D] " TAG ": " format "\n", ##__VA_ARGS__)
#define UI_LOGW(TAG, format, ...) printf("[W] " TAG ": " format "\n", ##__VA_ARGS__)
#define UI_LOGE(TAG, format, ...) printf("[E] " TAG ": " format "\n", ##__VA_ARGS__)

#else
#include "nvs.h"
#include "esp_log.h"
#include "data_manager.h"
#include "networking.h"
#include "tp_cal_screen.h"
#include "buzzer.h"
#include "tsc2046.h"
#include "mqtt_client_ha.h"

#define NVS_GENERAL_NAMESPACE "general"
#define NVS_SETUP_KEY "setup_done"

#define UI_LOGV(TAG, format, ...) ESP_LOGV(TAG, format, ##__VA_ARGS__)
#define UI_LOGI(TAG, format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
#define UI_LOGD(TAG, format, ...) ESP_LOGD(TAG, format, ##__VA_ARGS__)
#define UI_LOGW(TAG, format, ...) ESP_LOGW(TAG, format, ##__VA_ARGS__)
#define UI_LOGE(TAG, format, ...) ESP_LOGE(TAG, format, ##__VA_ARGS__)

#endif


void ui_init(void);
void ui_set_initialized(bool value);
void ui_set_network_initialized(bool value);
void ui_set_wifi_connected(bool value);
void ui_set_wifi_scan_result(ui_wifi_network_t * networks, uint16_t len);
void ui_set_servers_found(ui_server_t * servers, uint16_t len);

/**
 * @brief Check if the device is setup
 *
 * @return true if the device is setup, false otherwise
 */
static inline bool ui_is_setup(void) {
#if SIMULATOR
    return false;
#else
    uint8_t setup;

    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_GENERAL_NAMESPACE, NVS_READONLY, &nvs_handle));
    ESP_ERROR_CHECK(nvs_get_u8(nvs_handle, NVS_SETUP_KEY, &setup));
    nvs_close(nvs_handle);
    return (bool)setup;
#endif
}

/**
 * @brief Mark the device as setup, so the next time it boots it will go to the main screen
 *
 * @param done
 */
static inline void ui_set_setup_done(bool done) {
#if SIMULATOR
#else
    nvs_handle_t nvs_handle;
    nvs_open(NVS_GENERAL_NAMESPACE, NVS_READWRITE, &nvs_handle);
    nvs_set_u8(nvs_handle, NVS_SETUP_KEY, done);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
#endif
}


/**
 * @brief Get the power consumption
 *
 * @return The current power consumption in Watts
 */
static inline uint16_t ui_get_power_consumption(void)
{
#if SIMULATOR
    return lv_rand(0, 5000);
#else
    float current_power_usage;
    data_manager_get_field(DM_DF_CURRENT_POWER_USAGE, &current_power_usage);
    return (uint16_t)(current_power_usage*1000);
#endif
}

/**
 * @brief Get the current time
 *
 * @return The current time
 */
static inline time_t ui_get_time(void)
{
#if SIMULATOR
    return time(NULL);
#else
    time_t timestamp;
    data_manager_get_field(DM_DF_P1_TIMESTAMP, &timestamp);
    return timestamp;
#endif
}

/**
 * @brief Get the max peak of the month
 *
 * @return The max peak in Watts
 */
static inline uint16_t ui_get_max_peak_month(void)
{
#if SIMULATOR
    return lv_rand(1000, 5000) ;
#else
    data_manager_demand_data_point_t max_demand_month;
    data_manager_get_field(DM_DF_MAX_DEMAND_MONTH, &max_demand_month);
    if (max_demand_month.timestamp == 0 || max_demand_month.demand == 0)
    {
        return 2500;
    }
    return (uint16_t)(max_demand_month.demand*1000);
#endif
}

/**
 * @brief Get the predicted peak
 *
 * @return The predicted peak in Watts at the end of the current quarter hour
 */
static inline uint16_t ui_get_predicted_peak(void) {
#if SIMULATOR
    return lv_rand(1000, 5000) ;
#else
    data_manager_demand_data_point_t predicted_demand;
    data_manager_get_field(DM_DF_PREDICTED_PEAK, &predicted_demand);
    return (uint16_t) (predicted_demand.demand * 1000);
#endif
}

/**
 * @brief Connect to the configured wifi network
 *
 */
static inline void ui_connect_wifi(void) {
#if SIMULATOR

#else
    networking_wifi_connect();
#endif
}

/**
 * @brief Start the MQTT client
 */
static inline void ui_start_mqtt_client(void) {
#if SIMULATOR

#else
    esp_log_level_set("mqtt_client_ha", ESP_LOG_DEBUG);
    mqtt_client_init();
#endif
}

/**
 * @brief Start a scan for available wifi networks
 */
static inline void ui_scan_wifi_networks(void) {
#if SIMULATOR

#else
    networking_wifi_scan();
#endif
}

/**
 * @brief Set the wifi configuration
 *
 * @param[in] ssid The SSID of the wifi network
 * @param[in] password The password of the wifi network
 */
static inline void ui_set_wifi_config(const char *const ssid, const char *const password) {
#if SIMULATOR

#else
    networking_set_wifi_config(ssid, password);
#endif
}

/**
 * @brief Start a scan for available servers
 *
 * @note Server discovery is not implemented for the MQTT client yet.
 */
static inline void ui_find_servers(void) {
#if SIMULATOR

#else
    // TODO: discover the MQTT broker on the network
#endif
}

/**
 * @brief Save the given configuration
 *
 * @param[in] wifi_ssid The SSID of the wifi network
 * @param[in] wifi_passphrase The password of the wifi network
 * @param[in] server_hostname The hostname of the server
 */
static inline void ui_save_config(const char *const wifi_ssid, const char *const wifi_passphrase, const char *const server_hostname) {
#if SIMULATOR

#else
    (void) server_hostname;  // TODO: save the MQTT broker configuration

    ESP_ERROR_CHECK(networking_save_wifi_config(wifi_ssid, wifi_passphrase));
#endif
}

/**
 * @brief Reboot the device
 */
static inline void ui_reboot(void) {
#if SIMULATOR

#else
    // TODO: BUG: This causes a WIFI disconnect, which causes the event-handler to try to reconnect,
    //  but it can't because the network adapter is already deinitialized, so an assert is triggered
    esp_restart();
#endif
}

static inline void ui_beep_short(void) {
#if SIMULATOR

#else
    buzzer_beep(BUZZER_BEEP_TYPE_SHORT);
#endif
}

static inline void ui_beep_long(void) {
#if SIMULATOR

#else
    buzzer_beep(BUZZER_BEEP_TYPE_LONG);
#endif
}

static inline bool ui_is_touchscreen_calibrated(void) {
#if SIMULATOR
    return true;
#else
    return tsc2046_is_calibrated();
#endif
}

#endif //UI_H
