#include "esp_log.h"
#include "lvgl.h"
#include "data_manager.h"
#include "ui.h"

#define CHECK_INIT_PERIOD_MS 100
#define CHECK_INIT_TIMEOUT_MS 10000

static const char * TAG = "ui";
static bool initialized = false;
static bool network_initialized = false;
static bool setup_completed = false;
static bool wifi_connected = false;
static bool wifi_connected_tried = false;
static bool mqtt_client_started = false;
static lv_timer_t * loading_screen_timer;

// Function prototypes
static void check_loaded(lv_timer_t * timer);


/**
 * @brief Initialize the UI
 *
 * Show the loading screen until the initialization is complete (ui_set_initialized()) or the timeout is reached, then show the main screen
 *
 * @note The data manager must be initialized before calling this function
 */
void ui_init(void) {
    UI_LOGI(TAG, "Initializing UI");

//    tp_call_load_screen();
//    return;

    // Show the loading screen
    UI_LOGD(TAG, "Showing loading screen");
    loading_screen_init();
    lv_disp_load_scr(loading_screen);

    setup_completed = ui_is_setup();
    network_initialized = networking_is_initialized();


    if (ui_is_touchscreen_calibrated() == false) {
        UI_LOGD(TAG, "Touchscreen not calibrated, initializing calibration screen");
        tp_call_load_screen();
        return;
    }

    if (!setup_completed) {
        UI_LOGD(TAG, "Setup not completed, initializing setup screen");
        setup_screen_init();
    }

    main_screen_init();
    // Periodically check if the initialization is complete, and show the next screen if it is
    loading_screen_timer = lv_timer_create(check_loaded, CHECK_INIT_PERIOD_MS, NULL);

}

/**
 * @brief Set the initialization status
 *
 * @param[in] value true to set the initialization status to true, false otherwise
 */
void ui_set_initialized(const bool value) {
    initialized = value;
}

/**
 * @brief Set the network initialization status
 *
 * @param[in] value The network initialization status (true if the network is initialized)
 */
void ui_set_network_initialized(const bool value) {
    network_initialized = value;
}

/**
 * @brief Set the wifi connection status
 *
 * @param[in] value The wifi connection status (true if connected)
 */
void ui_set_wifi_connected(const bool value) {
    wifi_connected = value;
    wifi_connected_tried = false;

    if (wifi_connected && setup_completed && !mqtt_client_started) {
        UI_LOGD(TAG, "Starting the MQTT client");
        mqtt_client_started = true;
        ui_start_mqtt_client();
    }

    if (main_screen_initialized) {
        ui_set_wifi_status(value);
    }

    if (setup_screen_initialized) {
        setup_screen_wifi_connected_status_cb(value);
     }
}

/**
 * @brief Set the wifi scan result
 *
 * @param[in] networks The list of available wifi networks
 * @param[in] len The number of available wifi networks
 */
void ui_set_wifi_scan_result(ui_wifi_network_t * networks, uint16_t len) {
    if (setup_screen_initialized) {
        setup_screen_set_wifi_scan_result(networks, len);
    }
}

/**
 * @brief Set the available servers on the network
 *
 * @param[in] servers The list of available servers
 * @param[in] len The number of available servers
 */
void ui_set_servers_found(ui_server_t * servers, uint16_t len) {
    if (setup_screen_initialized) {
        setup_screen_set_servers_found(servers, len);
    }
}

/**
 * @brief Load the main screen after the initialization is complete or the timeout is reached
 *
 * This function is called periodically by a timer every CHECK_INIT_PERIOD_MS milliseconds
 *
 * @note The main screen must be initialized before calling this function
 *
 * @param timer
 */
static void check_loaded(lv_timer_t * timer) {
    static uint16_t counter = 0;

    if (setup_completed) {
        if (network_initialized) {
            // Connected to the network, if not already done
            if (!wifi_connected_tried && !wifi_connected) {
                wifi_connected_tried = true;
                ui_connect_wifi();
            }
        }
        // If the initialization is complete or the timeout is reached, show the main screen
        if (initialized || counter * CHECK_INIT_PERIOD_MS > CHECK_INIT_TIMEOUT_MS) {
            lv_timer_del(loading_screen_timer);
            UI_LOGD(TAG, "Showing main screen");
            lv_disp_load_scr(main_screen);
            loading_screen_deinit();
            counter = 0;
            return;
        }
        counter++;
    }
    else {
        if (network_initialized) {
            UI_LOGD(TAG, "Showing setup screen");
            lv_timer_del(loading_screen_timer);
            lv_disp_load_scr(setup_screen);
            loading_screen_deinit();
            counter = 0;
            return;
        }
    }
}
