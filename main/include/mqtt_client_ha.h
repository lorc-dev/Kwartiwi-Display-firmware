/**
 * @file mqtt_client_ha.h
 * @brief MQTT client that receives meter data from the Kwartiwi P1 server
 */

#ifndef MAIN_MQTT_CLIENT_HA_H
#define MAIN_MQTT_CLIENT_HA_H

#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(MQTT_CLIENT_HA_EVENTS);

typedef enum {
    MQTT_CLIENT_EVENT_CONNECTED,
    MQTT_CLIENT_EVENT_DISCONNECTED,
} mqtt_client_event_id_t;

/**
 * @brief Initialize and start the MQTT client
 *
 * Can be called multiple times; only the first call starts the client.
 */
void mqtt_client_init(void);

#endif // MAIN_MQTT_CLIENT_HA_H
