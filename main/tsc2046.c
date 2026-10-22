/**
 * @file tsc2046.c
 * @brief TSC2046 touch screen controller driver
 */

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "nvs.h"
#include "tsc2046.h"

// TSC2046 commands/control bytes (differential mode, 12-bit resolution, PD1=1, PD0=1)
#define CMD_Y_READ  0b10010011
#define CMD_X_READ  0b11010011
#define CMD_Z1_READ 0b10110011
#define CMD_Z2_READ 0b11000011

#define BUFFER_SIZE 5   // 2 * 2 * 1 = 2 measurements * 2 bytes/measurement + 1 (first(rx) or last(tx) byte is a 0 byte)
#define Z_THRESHOLD 100 // Minimum z value to be considered a valid touch
#define SAMPLE_COUNT 3  // Number of samples to average


static const char *TAG = "tsc2046";
static spi_device_handle_t tsc2046_spi_dev;
static SemaphoreHandle_t tsc2046_cal_data_mutex;

/**
 * Calibration data
 * X = cal_data[0] * x_raw + cal_data[1] * y_raw + cal_data[2]
 * Y = cal_data[3] * x_raw + cal_data[4] * y_raw + cal_data[5]
 */
static double cal_data[6];
static size_t cal_data_size = sizeof(cal_data);
static bool calibrated = false;

// Function prototypes
static esp_err_t read_cal_data_nvs(void);


/**
 * @brief Initialize the TSC2046 touch screen controller
 *
 * This function will setup the SPI bus and device, and read the calibration data from NVS.
 */
esp_err_t tsc2046_init(void) {
    ESP_LOGI(TAG, "Initializing TSC2046 touch screen controller");

    // Create mutex for cal data
    tsc2046_cal_data_mutex = xSemaphoreCreateMutex();
    if (tsc2046_cal_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex for cal data");
        return ESP_FAIL;
    }

    // Initialize the SPI bus
    spi_bus_config_t bus_cfg = {
        .miso_io_num = TSC2046_PIN_NUM_MISO,
        .mosi_io_num = TSC2046_PIN_NUM_MOSI,
        .sclk_io_num = TSC2046_PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 0,
        .flags = 0,
        .intr_flags = 0,
    };
    if(spi_bus_initialize(TSC2046_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus");
        return ESP_FAIL;
    }

    // Initialize the SPI device
    spi_device_interface_config_t dev_cfg = {
            .spics_io_num = TSC2046_PIN_NUM_CS,
            .clock_speed_hz = TSC2046_SPI_CLK_SPEED_HZ,
            .mode = 0,
            .flags = 0,
            .queue_size = 1,
            .pre_cb = NULL,
            .post_cb = NULL,
            .command_bits = 0,
            .address_bits = 0,
            .dummy_bits = 0,
    };
    if(spi_bus_add_device(TSC2046_SPI_HOST, &dev_cfg, &tsc2046_spi_dev) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate the tsc2046 device on the SPI bus");
        return ESP_FAIL;
    }

    // Read the calibration data from NVS
    if (read_cal_data_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read cal data from NVS");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Read the touch screen controller
 *
 * This function will read the touch screen controller.
 * If a valid touch is detected, it will return the X and Y coordinates.
 * If no touch is detected, it will return 0 for X and Y.
 * SAMPLE_COUNT + 1 samples will be taken. The first one will be discarded, and the remaining ones will be averaged.
 *
 * @warning This function is not thread safe. It should only be called from one task.
 *
 * @param[in] raw
 *   - true, return raw X and Y values.
 *   - false, return calibrated X and Y values.
 * @return The touchscreen status (x and y coordinates and pressed or not)
 */
tsc2046_data_t tsc2046_read(bool raw) {
    tsc2046_data_t result;
    const uint8_t tx_data_z[BUFFER_SIZE] =  {CMD_Z1_READ, 0, CMD_Z2_READ, 0, 0};
    const uint8_t tx_data_xy[BUFFER_SIZE] = {CMD_X_READ, 0, CMD_Y_READ, 0, 0};
    uint8_t rx_data[5];
    uint16_t z1;
    uint16_t z2;

    spi_transaction_t spi_tran = {
            .length = BUFFER_SIZE * 8,
            .rxlength = 0,
            .tx_buffer = tx_data_z,
            .rx_buffer = rx_data,
    };

    ESP_ERROR_CHECK(spi_device_polling_transmit(tsc2046_spi_dev, &spi_tran));
    z1 = ((rx_data[1] << 8) + rx_data[2]) >> 3;
    z2 = ((rx_data[3] << 8) + rx_data[4]) >> 3;

    result.x = 0;
    result.y = 0;
    result.state = TSC2046_STATE_RELEASED;

    // If the touch screen is not pressed, return
    if (z1 < Z_THRESHOLD || z2 > 4095 - Z_THRESHOLD) {
        return result;
    }

    // The touch screen is pressed, sample the x and y coordinates, and average the result
    result.state = TSC2046_STATE_PRESSED;
    spi_tran.tx_buffer = tx_data_xy;
    spi_tran.rx_buffer = rx_data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(tsc2046_spi_dev, &spi_tran));  // Discard the first sample as it is not accurate
    for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
        ESP_ERROR_CHECK(spi_device_polling_transmit(tsc2046_spi_dev, &spi_tran));
        result.x += ((rx_data[1] << 8) + rx_data[2]) >> 3;
        result.y += ((rx_data[3] << 8) + rx_data[4]) >> 3;
    }
    result.x /= SAMPLE_COUNT;
    result.y /= SAMPLE_COUNT;

    // Correct for orientation
    result.x = 4095 - result.x;
    result.y = 4095 - result.y;

    // Apply calibration data if not in raw mode
    if (!raw) {
        xSemaphoreTake(tsc2046_cal_data_mutex, portMAX_DELAY);
        result.x = (uint16_t)(cal_data[0] * (double)result.x + cal_data[1] * (double)result.y + cal_data[2]);
        result.y = (uint16_t)(cal_data[3] * (double)result.x + cal_data[4] * (double)result.y + cal_data[5]);
        xSemaphoreGive(tsc2046_cal_data_mutex);
    }

    ESP_LOGD(TAG, "Pressed: x: %d, y: %d", result.x, result.y);

    return result;
}

/**
 * @brief Calibrate the touch screen to the supplied calibration points
 *
 * This function will calibrate the touch screen controller using the supplied calibration points.
 * The calibration points should be evenly spaced across the screen.
 * At least 3 calibration points are required, 5 or more is recommended.
 *
 * This function uses a MMSE-based multi-point calibration algorithm.
 * @see https://www.analog.com/media/en/technical-documentation/application-notes/an-1021.pdf
 * @see https://www.embedded.com/how-to-calibrate-touch-screens/
 *
 * @note The calibration data will be applied temporarily, and will be lost when the device is reset.
 * To save the calibration data, call tsc2046_store_cal_data().
 *
 * @note This function modifies cal_data.
 *
 * @param[in] points The calibration points. (must be >= 3 points)
 * @param[in] num_points The number of calibration points supplied.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if less than 3 calibration points are supplied or if the calibration points are not valid (e.g. not evenly spaced)
 */
esp_err_t tsc2046_calibrate(const tsc2046_cal_data_point_t points[], const size_t num_points) {
    double a[3], b[3], c[3], d[3], k;

    if (num_points < 3) {
        ESP_LOGE(TAG, "Not enough calibration points, at least 3 are required");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < 3; i++) {
        a[i] = 0;
        b[i] = 0;
        c[i] = 0;
        d[i] = 0;
    }

    for (int i = 0; i < num_points; i++) {
        a[2] += (double)points[i].x_raw;
        b[2] += (double)points[i].y_raw;
        c[2] += (double)points[i].x_cal;
        d[2] += (double)points[i].y_cal;
        a[0] += (double)points[i].x_raw * (double)points[i].x_raw;
        a[1] += (double)points[i].x_raw * (double)points[i].y_raw;
        b[0] = a[1];
        b[1] += (double)points[i].y_raw * (double)points[i].y_raw;
        c[0] += (double)points[i].x_raw * (double)points[i].x_cal;
        c[1] += (double)points[i].y_raw * (double)points[i].x_cal;
        d[0] += (double)points[i].x_raw * (double)points[i].y_cal;
        d[1] += (double)points[i].y_raw * (double)points[i].y_cal;
    }

    if (a[2] == 0 || b[2] == 0) {
        ESP_LOGE(TAG, "Calibration failed, calibration points are not valid");
        return ESP_ERR_INVALID_ARG;
    }

    a[0] /= a[2];
    a[1] /= b[2];
    b[0] /= a[2];
    b[1] /= b[2];
    c[0] /= a[2];
    c[1] /= b[2];
    d[0] /= a[2];
    d[1] /= b[2];
    a[2] /= (double)num_points;
    b[2] /= (double)num_points;
    c[2] /= (double)num_points;
    d[2] /= (double)num_points;

    k = (a[0] - a[2]) * (b[1] - b[2])- (a[1] - a[2]) * (b[0] - b[2]);

    if (k == 0) {
        ESP_LOGE(TAG, "Calibration failed, calibration points are not valid");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(tsc2046_cal_data_mutex, portMAX_DELAY);

    cal_data[0] = (((c[0] - c[2]) * (b[1] - b[2])
                           - (c[1] - c[2]) * (b[0] - b[2])
                          ) / k);
    cal_data[1] = (((a[0] - a[2]) * (c[1] - c[2])
                           - (a[1] - a[2]) * (c[0] - c[2])
                          ) / k);
    cal_data[2] = ((b[0] * (a[2] * c[1] - a[1] * c[2])
                           + b[1] * (a[0] * c[2] - a[2] * c[0])
                           + b[2] * (a[1] * c[0] - a[0] * c[1])
                          ) / k);
    cal_data[3] = (((d[0] - d[2]) * (b[1] - b[2])
                           - (d[1] - d[2]) * (b[0] - b[2])
                          ) / k);
    cal_data[4] = (((a[0] - a[2]) * (d[1] - d[2])
                           - (a[1] - a[2]) * (d[0] - d[2])
                          ) / k);
    cal_data[5] = ((b[0] * (a[2] * d[1] - a[1] * d[2])
                           + b[1] * (a[0] * d[2] - a[2] * d[0])
                           + b[2] * (a[1] * d[0] - a[0] * d[1])
                          ) / k);
    ESP_LOGD(TAG, "Calibration data: %f %f %f %f %f %f", cal_data[0], cal_data[1], cal_data[2], cal_data[3], cal_data[4], cal_data[5]);
    calibrated = true;
    xSemaphoreGive(tsc2046_cal_data_mutex);

    return ESP_OK;
}

/**
 * @brief Store the calibration data in NVS
 *
 * The calibration data is stored as a blob, as NVS does not support storing floats or arrays of floats.
 *
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t tsc2046_store_cal_data(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS storage with the TSC2046 namespace
    if(nvs_open(TSC2046_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS with " TSC2046_NVS_NAMESPACE);
        return ESP_FAIL;
    }

    // Get the cal data semaphore
    xSemaphoreTake(tsc2046_cal_data_mutex, portMAX_DELAY);

    // Write the cal data to NVS
    err = nvs_set_blob(nvs_handle, TSC2046_NVS_KEY_CAL_DATA, cal_data, cal_data_size);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write calibration data to NVS");
        xSemaphoreGive(tsc2046_cal_data_mutex);
        nvs_close(nvs_handle);
        return ESP_FAIL;
    }

    // Commit the changes to NVS
    err = nvs_commit(nvs_handle);
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit calibration data to NVS");
        xSemaphoreGive(tsc2046_cal_data_mutex);
        nvs_close(nvs_handle);
        return ESP_FAIL;
    }

    // Release the cal data semaphore
    xSemaphoreGive(tsc2046_cal_data_mutex);

    // Close the NVS storage
    nvs_close(nvs_handle);

    return ESP_OK;
}

/**
 * @brief Check if the calibration data is loaded
 *
 * @return true if the calibration data is loaded, false otherwise
 */
bool tsc2046_is_calibrated(void) {
    bool ret;

    xSemaphoreTake(tsc2046_cal_data_mutex, portMAX_DELAY);
    ret = calibrated;
    xSemaphoreGive(tsc2046_cal_data_mutex);

    return ret;
}

/**
 * @brief Read the calibration data from NVS
 *
 * Read the calibration data from NVS.
 * If the calibration data is not stored in NVS, the raw X and Y values will be used.
 * The calibration data is stored as a blob in NVS.
 *
 * @return ESP_OK on success
 */
static esp_err_t read_cal_data_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS storage with the TSC2046 namespace
    if(nvs_open(TSC2046_NVS_NAMESPACE, NVS_READONLY, &nvs_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS with " TSC2046_NVS_NAMESPACE);
        return ESP_FAIL;
    }

    // Get the cal data semaphore
    xSemaphoreTake(tsc2046_cal_data_mutex, portMAX_DELAY);

    calibrated = false;

    // Read the cal data from NVS
    err = nvs_get_blob(nvs_handle, TSC2046_NVS_KEY_CAL_DATA, cal_data, &cal_data_size);

    // If the cal data is not stored in NVS, use the raw X and Y values
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Use the raw X and Y values (the other factors should be already zeroed)
        cal_data[0] = 1;
        cal_data[4] = 1;
        ESP_LOGI(TAG, "Calibration data NOT found, using raw data");
    }
    else if(err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading cal data from NVS: %d", err);
    }
    else {
        ESP_LOGI(TAG, "Calibration data found: %f, %f, %f, %f, %f, %f", cal_data[0], cal_data[1], cal_data[2], cal_data[3], cal_data[4], cal_data[5]);
        calibrated = true;
    }

    // Free resources
    xSemaphoreGive(tsc2046_cal_data_mutex);
    nvs_close(nvs_handle);

    return err;
}