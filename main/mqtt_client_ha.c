/**
 * @file mqtt_client_ha.c
 * @brief MQTT client that receives meter data from the Kwartiwi P1 server
 *
 * The client subscribes to the meter data topics and forwards the received
 * values to the data manager. The data manager notifies the UI when a new
 * meter data set is complete.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "data_manager.h"
#include "mqtt_client_ha.h"

// TODO: make the broker address configurable (NVS / setup screen)
#define SERVER_URI "mqtt://10.10.10.178"

#define POWER_TOPIC "kwartiwi/power"
#define CURRENT_AVG_DEMAND_TOPIC "kwartiwi/current_avg_demand"
#define MAX_DEMAND_MONTH_TOPIC "kwartiwi/max_demand_month"
#define AVG_DEMAND_PREDICTION_TOPIC "kwartiwi/avg_demand_prediction"
#define TIME_TOPIC "kwartiwi/time"
#define AVG_DEMAND_HISTORY_TOPIC "kwartiwi/avg_demand_history"

// The short term history covers 15 minutes. The server publishes 225 samples,
// which corresponds to one sample every 4 seconds.
#define AVG_DEMAND_HISTORY_INTERVAL_S 4

ESP_EVENT_DEFINE_BASE(MQTT_CLIENT_HA_EVENTS);

extern esp_event_loop_handle_t app_loop_handle;

static const char *TAG = "mqtt_client_ha";

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void log_error_if_nonzero(const char *message, int error_code);

/**
 * @brief Check if an MQTT topic matches the expected topic
 *
 * MQTT topics are not NUL-terminated, so the length has to be checked too.
 *
 * @param[in] topic The topic from the MQTT event
 * @param[in] topic_len The length of the topic
 * @param[in] expected The expected topic (NUL-terminated)
 * @return true if the topic matches, false otherwise
 */
static bool topic_matches(const char *topic, int topic_len, const char *expected) {
    const size_t expected_len = strlen(expected);

    return (size_t) topic_len == expected_len &&
           strncmp(topic, expected, expected_len) == 0;
}

/**
 * @brief Copy an MQTT payload into a NUL-terminated buffer
 *
 * @param[out] buffer The destination buffer
 * @param[in] buffer_size The size of the destination buffer
 * @param[in] data The payload (not NUL-terminated)
 * @param[in] len The length of the payload
 * @return true if the payload fits, false otherwise
 */
static bool copy_payload(char *buffer, size_t buffer_size, const char *data, size_t len) {
    if (len == 0 || len >= buffer_size) {
        return false;
    }

    memcpy(buffer, data, len);
    buffer[len] = '\0';

    return true;
}

/**
 * @brief Parse a scalar float payload
 *
 * @param[in] data The payload (not NUL-terminated)
 * @param[in] len The length of the payload
 * @param[out] value The parsed value
 * @return true on success, false otherwise
 */
static bool parse_float_payload(const char *data, size_t len, float *value) {
    char buffer[64];
    char *end = NULL;

    if (!copy_payload(buffer, sizeof(buffer), data, len)) {
        ESP_LOGW(TAG, "Invalid payload length: %d", (int) len);
        return false;
    }

    const float parsed = strtof(buffer, &end);
    if (end == buffer) {
        ESP_LOGW(TAG, "Failed to parse payload: %s", buffer);
        return false;
    }

    *value = parsed;
    return true;
}

/**
 * @brief Handle a new power value
 *
 * @param[in] data The payload (not NUL-terminated)
 * @param[in] len The length of the payload
 */
static void mqtt_power_data(const char *data, size_t len) {
    float power_kw = 0;

    if (!parse_float_payload(data, len, &power_kw)) {
        return;
    }

    data_manager_set_field(DM_DF_CURRENT_POWER_USAGE, &power_kw);
}

/**
 * @brief Handle a new average demand value
 *
 * @param[in] data The payload (not NUL-terminated)
 * @param[in] len The length of the payload
 */
static void mqtt_current_avg_demand_data(const char *data, size_t len) {
    float demand_kw = 0;

    if (!parse_float_payload(data, len, &demand_kw)) {
        return;
    }

    data_manager_set_field(DM_DF_CURRENT_AVG_DEMAND, &demand_kw);
}

/**
 * @brief Handle a new maximum demand of the month
 *
 * @param[in] data The payload (not NUL-terminated)
 * @param[in] len The length of the payload
 */
static void mqtt_max_demand_month_data(const char *data, size_t len) {
    float demand_kw = 0;

    if (!parse_float_payload(data, len, &demand_kw)) {
        return;
    }

    data_manager_demand_data_point_t max_demand_month = {
            .timestamp = 0,  // Timestamp is not published on this topic
            .demand = demand_kw,
    };

    data_manager_set_field(DM_DF_MAX_DEMAND_MONTH, &max_demand_month);
}

/**
 * @brief Handle a new predicted peak value
 *
 * @param[in] data The payload (not NUL-terminated)
 * @param[in] len The length of the payload
 */
static void mqtt_avg_demand_prediction_data(const char *data, size_t len) {
    float demand_kw = 0;

    if (!parse_float_payload(data, len, &demand_kw)) {
        return;
    }

    data_manager_demand_data_point_t predicted_peak = {
            .timestamp = 0,  // Timestamp is not published on this topic
            .demand = demand_kw,
    };

    data_manager_set_field(DM_DF_PREDICTED_PEAK, &predicted_peak);
}

/**
 * @brief Handle a new meter timestamp
 *
 * The timestamp also marks the end of a new meter data set, so the UI is
 * notified after it has been stored.
 *
 * @param[in] data The payload (not NUL-terminated)
 * @param[in] len The length of the payload
 */
static void mqtt_time_data(const char *data, size_t len) {
    char buffer[32];
    struct tm timestamp_tm = {0};

    if (!copy_payload(buffer, sizeof(buffer), data, len)) {
        ESP_LOGW(TAG, "Invalid timestamp length: %d", (int) len);
        return;
    }

    if (strptime(buffer, "%Y-%m-%d %H:%M:%S", &timestamp_tm) == NULL) {
        ESP_LOGW(TAG, "Failed to parse timestamp: %s", buffer);
        return;
    }

    timestamp_tm.tm_isdst = -1;
    time_t timestamp = mktime(&timestamp_tm);
    if (timestamp == (time_t) -1) {
        ESP_LOGW(TAG, "Failed to convert timestamp: %s", buffer);
        return;
    }

    data_manager_set_field(DM_DF_P1_TIMESTAMP, &timestamp);
    data_manager_notify_new_meter_data_available();
}

/**
 * @brief Handle the short term average demand history
 *
 * The payload is a comma-separated list of values, oldest first. The values
 * are stored with timestamps relative to the last known meter timestamp.
 *
 * @param[in] client The MQTT client handle
 * @param[in] data The payload (not NUL-terminated)
 * @param[in] len The length of the payload
 */
static void mqtt_avg_demand_history_data(esp_mqtt_client_handle_t client, const char *data, size_t len) {
    if (len == 0) {
        ESP_LOGW(TAG, "Empty history payload");
        return;
    }

    char *buffer = malloc(len + 1);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate history buffer");
        return;
    }

    memcpy(buffer, data, len);
    buffer[len] = '\0';

    // Remove trailing whitespace and separators, so the item count is correct
    while (len > 0 && (buffer[len - 1] == ',' || buffer[len - 1] == ' ' || buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
        buffer[--len] = '\0';
    }

    if (len == 0) {
        ESP_LOGW(TAG, "History payload contains no values");
        free(buffer);
        return;
    }

    // Count the values, so each value gets the correct timestamp
    size_t count = 1;
    for (size_t i = 0; i < len; i++) {
        if (buffer[i] == ',') {
            count++;
        }
    }

    time_t now = 0;
    data_manager_get_field(DM_DF_P1_TIMESTAMP, &now);
    if (now <= 0) {
        // TODO: the history should wait for a valid meter timestamp instead
        ESP_LOGW(TAG, "No meter timestamp available, using system time for history");
        now = time(NULL);
    }

    // Make sure the timestamps do not underflow when no valid clock is available
    const time_t history_span = (time_t) (count - 1) * AVG_DEMAND_HISTORY_INTERVAL_S;
    if (now < history_span) {
        now = history_span;
    }

    size_t index = 0;
    size_t parsed_count = 0;
    char *save_ptr = NULL;
    for (char *token = strtok_r(buffer, ",", &save_ptr);
         token != NULL && index < count;
         token = strtok_r(NULL, ",", &save_ptr)) {

        char *end = NULL;
        const float demand_kw = strtof(token, &end);

        if (end == token) {
            ESP_LOGW(TAG, "Failed to parse history value: %s", token);
            index++;
            continue;
        }

        const time_t timestamp = now - (time_t) (count - 1 - index) * AVG_DEMAND_HISTORY_INTERVAL_S;
        data_manager_add_max_demand_short_term_history_item(demand_kw, timestamp);
        parsed_count++;
        index++;
    }

    free(buffer);

    if (parsed_count == 0) {
        ESP_LOGW(TAG, "No valid history values received");
        return;
    }

    data_manager_notify_new_meter_history_data_available();

    // The history is only published once; stop listening to it after receiving it
    esp_mqtt_client_unsubscribe(client, AVG_DEMAND_HISTORY_TOPIC);
}

/**
 * @brief Handle a received MQTT data message
 *
 * @param[in] client The MQTT client handle
 * @param[in] event The MQTT event
 */
static void mqtt_data_handler(esp_mqtt_client_handle_t client, esp_mqtt_event_handle_t event) {
    if (topic_matches(event->topic, event->topic_len, POWER_TOPIC)) {
        mqtt_power_data(event->data, event->data_len);
    }
    else if (topic_matches(event->topic, event->topic_len, CURRENT_AVG_DEMAND_TOPIC)) {
        mqtt_current_avg_demand_data(event->data, event->data_len);
    }
    else if (topic_matches(event->topic, event->topic_len, MAX_DEMAND_MONTH_TOPIC)) {
        mqtt_max_demand_month_data(event->data, event->data_len);
    }
    else if (topic_matches(event->topic, event->topic_len, AVG_DEMAND_PREDICTION_TOPIC)) {
        mqtt_avg_demand_prediction_data(event->data, event->data_len);
    }
    else if (topic_matches(event->topic, event->topic_len, TIME_TOPIC)) {
        mqtt_time_data(event->data, event->data_len);
    }
    else if (topic_matches(event->topic, event->topic_len, AVG_DEMAND_HISTORY_TOPIC)) {
        mqtt_avg_demand_history_data(client, event->data, event->data_len);
    }
    else {
        ESP_LOGW(TAG, "Unhandled topic: %.*s", event->topic_len, event->topic);
    }
}

/**
 * @brief Subscribe to all meter data topics after the client connected
 *
 * @param[in] client The MQTT client handle
 */
static void mqtt_connected_handler(esp_mqtt_client_handle_t client) {
    static const char *const topics[] = {
            POWER_TOPIC,
            CURRENT_AVG_DEMAND_TOPIC,
            MAX_DEMAND_MONTH_TOPIC,
            AVG_DEMAND_PREDICTION_TOPIC,
            TIME_TOPIC,
            AVG_DEMAND_HISTORY_TOPIC,
    };

    for (size_t i = 0; i < sizeof(topics) / sizeof(topics[0]); i++) {
        if (esp_mqtt_client_subscribe(client, topics[i], 0) < 0) {
            ESP_LOGW(TAG, "Failed to subscribe to %s", topics[i]);
        }
    }

    ESP_ERROR_CHECK(esp_event_post_to(app_loop_handle, MQTT_CLIENT_HA_EVENTS,
                                      MQTT_CLIENT_EVENT_CONNECTED, NULL, 0, portMAX_DELAY));
}

/**
 * @brief Notify the application that the client disconnected
 *
 * The MQTT client reconnects automatically, so no retry is scheduled here.
 *
 * @param[in] client The MQTT client handle (unused)
 */
static void mqtt_disconnected_handler(esp_mqtt_client_handle_t client) {
    (void) client;

    ESP_ERROR_CHECK(esp_event_post_to(app_loop_handle, MQTT_CLIENT_HA_EVENTS,
                                      MQTT_CLIENT_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY));
}

/**
 * @brief Initialize and start the MQTT client
 *
 * Can be called multiple times; only the first call starts the client.
 */
void mqtt_client_init(void) {
    static bool initialized = false;

    if (initialized) {
        ESP_LOGW(TAG, "MQTT client already initialized");
        return;
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = SERVER_URI,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }
    initialized = true;

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));

    ESP_LOGI(TAG, "MQTT client started, broker: %s", SERVER_URI);
}

/**
 * @brief Log an error if the error code is non-zero
 *
 * @param[in] message The error message
 * @param[in] error_code The error code
 */
static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/**
 * @brief Event handler registered to receive MQTT events
 *
 * This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event
 * @param base Event base for the handler (always MQTT_EVENTS)
 * @param event_id The id for the received event
 * @param event_data The data for the event, esp_mqtt_event_handle_t
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    (void) handler_args;

    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);

    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_connected_handler(client);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_disconnected_handler(client);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT_EVENT_DATA, topic: %.*s", event->topic_len, event->topic);
            mqtt_data_handler(client, event);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;

        default:
            ESP_LOGD(TAG, "Unhandled MQTT event id: %d", event->event_id);
            break;
    }
}
