/**
 * @file networking.c
 * @brief Networking functions
 * @todo  - Add support for static IP configuration in STA mode
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mdns.h"
#include "networking.h"

// Bit definitions for the Wi-Fi event group
#define WIFI_STA_CONNECTED_BIT  BIT0
#define WIFI_STA_FAIL_BIT       BIT1
#define WIFI_STA_STARTED_BIT    BIT2
#define WIFI_SCAN_DONE_BIT      BIT3

// Bit definitions for the networking event group
#define NETWORKING_INITIALIZED_BIT BIT0

ESP_EVENT_DEFINE_BASE(NETWORKING_EVENTS);           // Networking events base
extern esp_event_loop_handle_t app_loop_handle;

static const char *TAG = "networking";              // Tag used for logging
static EventGroupHandle_t s_wifi_event_group;       // Event group used in STA mode
static EventGroupHandle_t networking_event_group;   // Event group for networking events
static esp_netif_t *network_interface;              // Network interface handle (only one interface is supported for now)
static SemaphoreHandle_t networking_config_mutex;   // Mutex for accessing the networking_config struct
static esp_event_handler_instance_t instance_wifi_any_id;   // Event handler instance for any Wi-Fi event
static esp_event_handler_instance_t instance_ip_got_ip;     // Event handler instance for IP_GOT_IP event
static struct {                                     // Configuration
    wifi_config_t wifi_config;
    char hostname[NETWORKING_HOSTNAME_MAX_LEN];
    char mdns_instance_name[NETWORKING_HOSTNAME_MAX_LEN];
} networking_config;


// Function prototypes
static esp_err_t read_config_from_nvs(void);
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void wifi_init_sta(wifi_config_t *config);
static void init_mdns(void);


/**
 * @brief Initialize Wi-Fi in Station mode
 *
 * Setup a network interface, based on the configuration in NVS.
 *
 * @note The default and app event loops must be running before calling this function.
 * @note This function will modify the global networking_config struct.
 * @warning This function is not thread-safe.
 */
void setup_networking(void) {
    wifi_config_t *wifi_config = &networking_config.wifi_config;

    ESP_LOGI(TAG, "Setting up networking");

    networking_config_mutex = xSemaphoreCreateMutex();
    assert(networking_config_mutex != NULL);

    // Create the event group
    networking_event_group = xEventGroupCreate();
    assert(networking_event_group != NULL);

    // Create the event group for Wi-Fi events
    s_wifi_event_group = xEventGroupCreate();
    assert(s_wifi_event_group != NULL);

    // Read configuration from NVS
    ESP_ERROR_CHECK(read_config_from_nvs());

    // Initialize the TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    xSemaphoreTake(networking_config_mutex, portMAX_DELAY);

    // Initialize in station mode
    wifi_init_sta(wifi_config);

    // Set the hostname
    ESP_LOGD(TAG, "Setting hostname to %s", networking_config.hostname);
    ESP_ERROR_CHECK(esp_netif_set_hostname(network_interface, networking_config.hostname));

    // Initialize mDNS
    init_mdns();

    xSemaphoreGive(networking_config_mutex);

    // Notify the rest of the system that networking is initialized
    ESP_ERROR_CHECK(esp_event_post_to(app_loop_handle, NETWORKING_EVENTS, NETWORKING_INITIALIZED, NULL, 0, portMAX_DELAY));
    xEventGroupSetBits(networking_event_group, NETWORKING_INITIALIZED_BIT);
    ESP_LOGD(TAG, "Networking initialized");
}

/**
 * @brief Check if networking is initialized
 *
 * @return True if networking is initialized, false otherwise
 */
bool networking_is_initialized(void) {
    if (networking_event_group == NULL) {
        return false;
    }
    return xEventGroupGetBits(networking_event_group) & NETWORKING_INITIALIZED_BIT;
}

/**
 * @brief Set the ssid and password for the Wi-Fi network to connect to
 *
 *
 * @param[in] ssid The SSID of the Wi-Fi network (max 31 characters + null terminator)
 * @param[in] password The passphrase of the Wi-Fi network (max 63 characters + null terminator)
 */
void networking_set_wifi_config(const char * const ssid, const char * const password) {
    wifi_config_t *wifi_config = &networking_config.wifi_config;

    // Check that the parameters are valid
    assert(ssid != NULL && strlen(ssid) < sizeof(wifi_config->sta.ssid));
    assert(password != NULL && strlen(password) < sizeof(wifi_config->sta.password));

    xSemaphoreTake(networking_config_mutex, portMAX_DELAY);

    // Set the new configuration
    strcpy((char *)wifi_config->sta.ssid, ssid);
    strcpy((char *)wifi_config->sta.password, password);

    xSemaphoreGive(networking_config_mutex);
}

/**
 * @brief Save the Wi-Fi configuration to NVS
 *
 * If ssid or password is NULL, the current configuration will be saved.
 *
 * @param[in] ssid The SSID of the Wi-Fi network (max 31 characters + null terminator)
 * @param[in] password The passphrase of the Wi-Fi network (max 63 characters + null terminator)
 * @return ESP_OK on success, ESP_ERR_NVS_* on failure
 */
esp_err_t networking_save_wifi_config(const char * ssid, const char * password) {
    // Save the settings to NVS
    nvs_handle_t nvs_handle;
    esp_err_t err;


    // Open Non-volatile storage
    err = nvs_open(NETWORKING_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS");
        return err;
    }

    xSemaphoreTake(networking_config_mutex, portMAX_DELAY);

    // Use the current configuration if ssid or password is NULL
    if (ssid == NULL || password == NULL) {
        ssid = (char *)networking_config.wifi_config.sta.ssid;
        password = (char *)networking_config.wifi_config.sta.password;
    }

    // SSID
    err = nvs_set_str(nvs_handle, NETWORKING_NVS_KEY_STA_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write STA SSID to NVS");
        nvs_close(nvs_handle);
        xSemaphoreGive(networking_config_mutex);
        return err;
    }
    // Password
    err = nvs_set_str(nvs_handle, NETWORKING_NVS_KEY_STA_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write STA password to NVS");
        nvs_close(nvs_handle);
        xSemaphoreGive(networking_config_mutex);
        return err;
    }

    // Commit the changes to NVS
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit changes to NVS");
        nvs_close(nvs_handle);
        xSemaphoreGive(networking_config_mutex);
        return err;
    }

    xSemaphoreGive(networking_config_mutex);

    // Close NVS
    nvs_close(nvs_handle);

    return ESP_OK;
}

/**
 * @brief Start a Wi-Fi scan
 *
 * When the scan is complete, the NETWORKING_WIFI_SCAN_COMPLETE event will be posted to the app_loop task.
 * The results of the scan can be read using esp_wifi_scan_get_ap_records().
 *
 * @note Wait until the NETWORKING_WIFI_SCAN_COMPLETE event is posted before reading the results of the scan, or calling this function again.
 */
void networking_wifi_scan(void) {
    ESP_LOGI(TAG, "Scanning for networks");
    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, false));
}

/**
 * @brief Connect to the Wi-Fi network
 *
 * Connect to the Wi-Fi network specified in the configuration.
 * To set the Wi-Fi configuration, call networking_set_wifi_config().
 *
 * This function is non-blocking. It returns immediately after the connection attempt has been started.
 * The connection attempt may fail. In that case, the NETWORKING_EVENT_WIFI_DISCONNECTED event will be posted.
 * If the connection attempt succeeds, the NETWORKING_EVENT_WIFI_CONNECTED event will be posted.
 *
 * @note Wait until the NETWORKING_EVENT_WIFI_DISCONNECTED or NETWORKING_EVENT_WIFI_CONNECTED event is posted before calling this function again.
 * @note When this function returns ESP_FAIL, the connection attempt will not be started and no event will be posted.
 * @todo Disconnect if already connected
 *
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t networking_wifi_connect(void) {
    wifi_config_t *config = &networking_config.wifi_config;

    if (xSemaphoreTake(networking_config_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "networking_wifi_connect : Failed to take networking_config_mutex");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "networking_wifi_connect : Connecting to SSID: %s", config->sta.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    xSemaphoreGive(networking_config_mutex);

    return ESP_OK;
}

/**
 * @brief Read network configuration from NVS
 *
 * @note This function will write to the global networking_config variable
 * @note NVS must be initialized before calling this function
 * @note the hostname and mDNS instance name will be set, even if the function returns an error. If the hostname or mDNS instance name could not be read from NVS, this function will call abort().
 *
 * @return ESP_OK on success, ESP_FAIL if the ssid or password could not be read
 */
static esp_err_t read_config_from_nvs(void) {
    nvs_handle_t nvs_handle;
    size_t max_len;
    esp_err_t err = ESP_OK;
    wifi_config_t *wifi_config = &networking_config.wifi_config;

    ESP_LOGD(TAG, "Reading Wi-Fi configuration from NVS");

    // Open Non-volatile storage
    ESP_ERROR_CHECK(nvs_open(NETWORKING_NVS_NAMESPACE, NVS_READONLY, &nvs_handle));

    xSemaphoreTake(networking_config_mutex, portMAX_DELAY);

    // Read the configuration for STA mode
    max_len = sizeof(wifi_config->sta.ssid);
    if(nvs_get_str(nvs_handle, NETWORKING_NVS_KEY_STA_SSID, (char *) wifi_config->sta.ssid, &max_len) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read STA SSID from NVS");
        err = ESP_FAIL;
    }
    max_len = sizeof(wifi_config->sta.password);
    if(nvs_get_str(nvs_handle, NETWORKING_NVS_KEY_STA_PASS, (char *) wifi_config->sta.password, &max_len) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read STA password from NVS");
        err = ESP_FAIL;
    }

    // Read the hostname
    max_len = sizeof(networking_config.hostname);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NETWORKING_NVS_KEY_HOSTNAME, networking_config.hostname, &max_len));

    // Read the mDNS instance name
    max_len = sizeof(networking_config.mdns_instance_name);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NETWORKING_NVS_KEY_MDNS_INSTANCE_NAME, networking_config.mdns_instance_name, &max_len));

    xSemaphoreGive(networking_config_mutex);

    // Close the nvs handle now that all config data has been read
    nvs_close(nvs_handle);

    return err;
}

/**
 * @brief Wifi station event handler
 *
 * @param[in] arg
 * @param[in] event_base
 * @param[in] event_id
 * @param[in] event_data
 */
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    static uint8_t retry_count = 0;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA started");
        xEventGroupSetBits(s_wifi_event_group, WIFI_STA_STARTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < NETWORKING_WIFI_STA_CONN_RETRIES) {
            retry_count++;
            ESP_LOGI(TAG, "Retrying to connect to the AP. Attempt %d/%d", retry_count, NETWORKING_WIFI_STA_CONN_RETRIES);
            ESP_ERROR_CHECK(esp_wifi_connect());
        }
        else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_STA_FAIL_BIT);
            retry_count = 0;
            ESP_ERROR_CHECK(esp_event_post_to(app_loop_handle, NETWORKING_EVENTS, NETWORKING_EVENT_WIFI_DISCONNECTED, NULL, 0, portMAX_DELAY));
        }
        ESP_LOGI(TAG,"Failed to connect to the AP");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Wi-Fi scan done");
        xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
        ESP_ERROR_CHECK(esp_event_post_to(app_loop_handle, NETWORKING_EVENTS, NETWORKING_EVENT_WIFI_SCAN_DONE, NULL, 0, portMAX_DELAY));
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Connected to AP. IP address: "IPSTR, IP2STR(&((ip_event_got_ip_t*)event_data)->ip_info.ip));
        retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_STA_CONNECTED_BIT);
        ESP_ERROR_CHECK(esp_event_post_to(app_loop_handle, NETWORKING_EVENTS, NETWORKING_EVENT_WIFI_CONNECTED, NULL, 0, portMAX_DELAY));
    }
}

/**
 * @brief Initialize Wi-Fi in Station mode
 *
 * @param[in, out] config The Wi-Fi configuration. sta.ssid and sta.password must be set.
 */
static void wifi_init_sta(wifi_config_t *config) {
    ESP_LOGI(TAG, "Setting up Wi-Fi in STA mode");

    // Set up Wi-Fi in Station mode
    network_interface =  esp_netif_create_default_wifi_sta();

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, NULL, &instance_wifi_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_sta_event_handler, NULL, &instance_ip_got_ip));

    // Set the Wi-Fi configuration
    config->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config->sta.pmf_cfg.capable = true;
    config->sta.pmf_cfg.required = false;
#ifdef NETWORKING_WIFI_STA_WPA2_ONLY
    config->sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
#endif

    // Start Wi-Fi in Station mode with the given configuration
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGD(TAG, "Starting Wi-Fi");

    EventBits_t event_bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_STA_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
    if (!(event_bits & WIFI_STA_STARTED_BIT)) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi in STA mode");
        abort();
    }
}

/**
 * @brief Deinitialize Wi-Fi in Station mode
 */
static void wifi_deinit_sta(void) {
    ESP_LOGI(TAG, "Deinitializing Wi-Fi in STA mode");

    // Unregister event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_ip_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_wifi_any_id));

    // Stop Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    // Destroy the network interface
    esp_netif_destroy(network_interface);
}

/**
 * @brief Initialize mDNS
 *
 * @note This function uses the hostname and mdns_instance_name fields of the global networking_config struct.
 */
static void init_mdns(void) {
    ESP_LOGI(TAG, "Initializing mDNS");
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(networking_config.hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set(networking_config.mdns_instance_name));
}