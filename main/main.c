#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_event.h"
#include "cJSON.h"
#include "networking.h"
#include "buzzer.h"
#include "ui_task.h"
#include "tsc2046.h"
#include "data_manager.h"

esp_event_loop_handle_t app_loop_handle;

static void * CJSON_CDECL cjson_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}


void app_main(void) {

    //Initialize NVS
    esp_err_t err = nvs_flash_init();
    // Check if NVS partition was truncated
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Erase NVS partition and try again
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialize the event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_event_loop_args_t loop_args = {
            .queue_size = 5,
            .task_name = "event_loop_task",
            .task_priority = 5,
            .task_stack_size = 4096,
            .task_core_id = 0
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &app_loop_handle));

    // Initialize the CJson library to use the psram
    static cJSON_Hooks hooks = {
            .malloc_fn = cjson_malloc,
            .free_fn = heap_caps_free,
    };
    cJSON_InitHooks(&hooks);

    // Initialize the buzzer
    esp_log_level_set("buzzer", ESP_LOG_DEBUG);
    xTaskCreate(&buzzer_task, "buzzer_task", 4096, NULL, 5, NULL);

    // Initialize the touchscreen
    esp_log_level_set("tsc2046", ESP_LOG_INFO);
    tsc2046_init();

    // Init data manager
    esp_log_level_set("data_manager", ESP_LOG_DEBUG);
    data_manager_init();

    // Initialize the UI
    esp_log_level_set("ui_task", ESP_LOG_DEBUG);
    esp_log_level_set("ui", ESP_LOG_DEBUG);
    xTaskCreatePinnedToCore(&ui_task, "ui_task", 8192, NULL, 6, NULL, 1);

    // Initialize networking
    esp_log_level_set("networking", ESP_LOG_DEBUG);
    setup_networking();
}
