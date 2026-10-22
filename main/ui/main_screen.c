/**
 * @file main_screen.c
 * @brief Main screen
 *
 * This screen is the main screen of the application.
 * It shows the current power consumption, the current time, the predicted peak and the peak demand chart.
 *
 * The following functions can be used to set the data this screen uses:
 *   - ui_set_power_consumption(uint16_t value)
 *   - ui_set_new_max_peak_demand(uint16_t value)
 *   - ui_reset_peak_demand_chart_data(void)
 *   - ui_add_peak_demand_data_point(time_t time, uint16_t value)
 *   - ui_set_predicted_peak(uint16_t value)
 *   - ui_set_wifi_status(bool connected)
 *   - ui_set_connected_status(bool connected)
 *
 * @todo Load the settings screen when the icon is pressed instead of the touch cal screen
 * @todo Add helper functions to set the energy chart data
 * @todo Show the correct time of the day under the energy chart
 */

#include <stdio.h>
#include <assert.h>
#include "lvgl.h"
#include "ui.h"
#include "main_screen.h"

// UI constants
#define MAX_PEAK_DEMAND_MIN_VALUE_W 2500
#define PEAK_DEMAND_CHART_HEIGHT_PX 110
#define PEAK_DEMAND_CHART_DEFAULT_Y_RANGE (MAX_PEAK_DEMAND_MIN_VALUE_W)
#define PEAK_DEMAND_CHART_POINT_COUNT 225
#define PEAK_DEMAND_CHART_PADDING_PX 7 // (240 - 225 - 1) / 2
#define MAX_PEAK_LINE_DEFAULT_POS MAX_PEAK_DEMAND_MIN_VALUE_W
#define MAX_PEAK_LINE_MIN_OFFSET_TOP_PX 20
#define ENERGY_CHART_HEIGHT_PX 70

// Colors
#define TEXT_COLOR lv_color_hex(0xFFFFFF)
#define BACKGROUND_COLOR lv_color_hex(0x000000)

bool main_screen_initialized = false;

// LVGL objects
lv_obj_t * main_screen;
static lv_obj_t * power_consumption_label;
static lv_obj_t * power_consumption_unit_label;
static lv_obj_t * energy_chart;
static lv_obj_t * peak_demand_chart;
static lv_chart_series_t * peak_demand_chart_series;
static lv_chart_series_t * energy_chart_series;
static lv_obj_t * time_label;
static lv_obj_t * wifi_symbol_img;
static lv_obj_t * connected_symbol_img;
static lv_obj_t * settings_symbol_img;
static lv_obj_t * max_peak_line;
static lv_obj_t * max_peak_label;
static lv_obj_t * predicted_peak_line;

// Static variables
static lv_timer_t * timer_1s;
static lv_timer_t * alarm_timer;
static lv_coord_t peak_demand_chart_y_range = PEAK_DEMAND_CHART_DEFAULT_Y_RANGE;
static lv_point_t peak_demand_last_point_w = {0, 0};
static lv_coord_t max_peak_line_pos_w = MAX_PEAK_LINE_DEFAULT_POS;
static lv_point_t predicted_peak_line_points[2] = {{0, PEAK_DEMAND_CHART_HEIGHT_PX}, {0, PEAK_DEMAND_CHART_HEIGHT_PX}};
static uint16_t new_max_peak_demand_w = MAX_PEAK_LINE_DEFAULT_POS;

// Fonts and images
LV_FONT_DECLARE(roboto_bold_70);
LV_FONT_DECLARE(roboto_bold_20);
LV_IMG_DECLARE(wifi_symbol_20_14);
LV_IMG_DECLARE(wifi_symbol_nc_20_14);
LV_IMG_DECLARE(connected_symbol_14_14);
LV_IMG_DECLARE(connected_symbol_nc_14_14);
LV_IMG_DECLARE(settings_symbol_20_20);

// Function prototypes
static void set_peak_demand_chart_data_point(uint16_t value, uint8_t index);
static void show_alarm_style(bool alarm);
static void set_alarm_status(bool alarm);
static void peak_demand_chart_draw_event_cb(lv_event_t * e);
static void energy_chart_draw_event_cb(lv_event_t * e);
static void open_settings_event_cb(lv_event_t * e);
static void timer_1s_cb(lv_timer_t * timer);
static void alarm_timer_cb(lv_timer_t * timer);

/**
 * @brief Initialize the main screen
 */
void main_screen_init(void)
{
    main_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(main_screen, BACKGROUND_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);

    // power consumption label
    power_consumption_label = lv_label_create(main_screen);
    lv_obj_set_width(power_consumption_label, LV_SIZE_CONTENT);
    lv_obj_set_height(power_consumption_label, LV_SIZE_CONTENT);
    lv_obj_align(power_consumption_label, LV_ALIGN_CENTER, -10, 20);
    lv_obj_set_style_text_color(power_consumption_label, TEXT_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(power_consumption_label, &roboto_bold_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(power_consumption_label, "0");

    // power consumption unit label
    power_consumption_unit_label = lv_label_create(main_screen);
    lv_obj_set_width(power_consumption_unit_label, LV_SIZE_CONTENT);
    lv_obj_set_height(power_consumption_unit_label, LV_SIZE_CONTENT);
    lv_obj_align_to(power_consumption_unit_label, power_consumption_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);
    lv_obj_set_style_text_color(power_consumption_unit_label, TEXT_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(power_consumption_unit_label, &roboto_bold_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(power_consumption_unit_label, "kW");

    // energy chart
    energy_chart = lv_chart_create(main_screen);
    lv_obj_set_width(energy_chart, LV_HOR_RES);
    lv_obj_set_height(energy_chart, ENERGY_CHART_HEIGHT_PX);
    lv_obj_align(energy_chart, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_chart_set_type(energy_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(energy_chart, 45);
    lv_chart_set_div_line_count(energy_chart,0,0);
    lv_chart_set_axis_tick(energy_chart, LV_CHART_AXIS_PRIMARY_X, 2, 0, 5, 1, true, 20);
    lv_chart_set_axis_tick(energy_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 0, 0, false, 20);
    lv_chart_set_axis_tick(energy_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 0, 0, 0, false, 20);
    lv_obj_set_style_pad_column(energy_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(energy_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(energy_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(energy_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_pad(energy_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(energy_chart, &lv_font_montserrat_12, LV_PART_TICKS | LV_STATE_DEFAULT);
    energy_chart_series = lv_chart_add_series(energy_chart, COLOR_KWARTIWI_ORANGE, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_event_cb(energy_chart, energy_chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    for (int i = 0; i < 45; i++) {
        lv_chart_set_next_value(energy_chart, energy_chart_series, lv_rand(0, 100));
    }

    // peak demand chart
    peak_demand_chart = lv_chart_create(main_screen);
    lv_obj_set_size(peak_demand_chart, LV_HOR_RES, PEAK_DEMAND_CHART_HEIGHT_PX);
    lv_obj_align(peak_demand_chart, LV_ALIGN_TOP_MID, 0, 25);
    lv_chart_set_type(peak_demand_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(peak_demand_chart, 0, 0);
    lv_chart_set_range(peak_demand_chart,LV_CHART_AXIS_PRIMARY_Y, 0, peak_demand_chart_y_range);
    lv_obj_set_style_bg_opa(peak_demand_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(peak_demand_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_size(peak_demand_chart, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(peak_demand_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(peak_demand_chart, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(peak_demand_chart, PEAK_DEMAND_CHART_PADDING_PX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(peak_demand_chart, PEAK_DEMAND_CHART_PADDING_PX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_chart_set_point_count(peak_demand_chart, PEAK_DEMAND_CHART_POINT_COUNT);
    lv_obj_add_event_cb(peak_demand_chart, peak_demand_chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    peak_demand_chart_series = lv_chart_add_series(peak_demand_chart, COLOR_WHITE, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(peak_demand_chart, peak_demand_chart_series, LV_CHART_POINT_NONE);

    // max peak line
    max_peak_line = lv_obj_create(peak_demand_chart);
    lv_obj_set_size(max_peak_line, LV_HOR_RES, 1);
    lv_obj_set_align(max_peak_line, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_pos(max_peak_line, 0, 0);
    lv_obj_set_style_bg_color(max_peak_line, COLOR_KWARTIWI_RED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(max_peak_line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(peak_demand_chart, LV_OBJ_FLAG_SCROLLABLE);

    // max peak label
    max_peak_label = lv_label_create(peak_demand_chart);
    lv_obj_set_style_text_color(max_peak_label, COLOR_KWARTIWI_RED, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(max_peak_label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(max_peak_label, max_peak_line, LV_ALIGN_OUT_TOP_LEFT, 10, 0);
    lv_label_set_text(max_peak_label, "0 W");

    // Predicted peak line
    predicted_peak_line = lv_line_create(peak_demand_chart);
    lv_line_set_points(predicted_peak_line, predicted_peak_line_points, 1);
    lv_obj_set_style_line_color(predicted_peak_line, COLOR_KWARTIWI_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_width(predicted_peak_line, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Date time label
    time_label = lv_label_create(main_screen);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_color(time_label, TEXT_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(time_label, "00:00");

    // Wifi symbol img
    wifi_symbol_img = lv_img_create(main_screen);
    lv_img_set_src(wifi_symbol_img, &wifi_symbol_nc_20_14);
    lv_obj_set_size(wifi_symbol_img, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(wifi_symbol_img, LV_ALIGN_TOP_LEFT, 5, 5);

    // Connected symbol img
    connected_symbol_img = lv_img_create(main_screen);
    lv_img_set_src(connected_symbol_img, &connected_symbol_nc_14_14);
    lv_obj_set_size(connected_symbol_img, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(connected_symbol_img, LV_ALIGN_TOP_LEFT, 25, 5);

    // Settings symbol img
    settings_symbol_img = lv_img_create(main_screen);
    lv_img_set_src(settings_symbol_img, &settings_symbol_20_20);
    lv_obj_set_size(settings_symbol_img, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(settings_symbol_img, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_add_flag(settings_symbol_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings_symbol_img, open_settings_event_cb, LV_EVENT_CLICKED, NULL);

    // Timers
    timer_1s = lv_timer_create(timer_1s_cb, 1000, NULL);
    alarm_timer = lv_timer_create(alarm_timer_cb, 500, NULL);

    // Set the initial values
    new_max_peak_demand_w = ui_get_max_peak_month();
    ui_set_max_peak_line(new_max_peak_demand_w);
    set_alarm_status(false);

    main_screen_initialized = true;
}

/**
 * @brief Set the power consumption
 *
 * This function will update the power consumption label and unit label, according to the value.
 *
 * @param[in] value The power consumption value in Watt
 */
void ui_set_power_consumption(uint16_t value) {
    if (value < 9999) {
        lv_label_set_text_fmt(power_consumption_label, "%d", value);
        lv_label_set_text(power_consumption_unit_label, "W");
    }else{
        lv_label_set_text_fmt(power_consumption_label, "%d.%d", value/1000, value%1000);
        lv_label_set_text(power_consumption_unit_label, "kW");
    }
    lv_obj_align_to(power_consumption_unit_label, power_consumption_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);
}

/**
 * @brief Set the time
 *
 * This function will update the time label, according to the value.
 *
 * @param[in] time The current time
 */
void ui_set_time(time_t time) {
    struct tm * _tm = localtime(&time);
    lv_label_set_text_fmt(time_label, "%02d:%02d", _tm->tm_hour, _tm->tm_min);
}

/**
 * @brief Set the max peak line
 *
 * This function will update the max peak line label and the max peak line, according to the value.
 * The max peak line will always be at the top of the chart, except when there is a higher value in the chart.
 *
 * @note When the data in the chart is changed, this function should also be called so the chart is correctly scaled.
 *
 * @param[in] value The new max peak value in Watt
 */
void ui_set_max_peak_line(uint16_t value) {
    if (value < MAX_PEAK_DEMAND_MIN_VALUE_W) {
        value = MAX_PEAK_DEMAND_MIN_VALUE_W;
    }
    // Find the highest point in the chart in Watt, and add an offset in px to it
    lv_coord_t highest_point = (lv_coord_t)(value > peak_demand_last_point_w.y ? value : peak_demand_last_point_w.y);
    highest_point = (lv_coord_t)(highest_point * PEAK_DEMAND_CHART_HEIGHT_PX / (PEAK_DEMAND_CHART_HEIGHT_PX - MAX_PEAK_LINE_MIN_OFFSET_TOP_PX));

    // Update the chart range so that the highest point is at the top of the chart
    peak_demand_chart_y_range = highest_point;
    lv_chart_set_range(peak_demand_chart, LV_CHART_AXIS_PRIMARY_Y, 0, peak_demand_chart_y_range);

    // Update the max peak line (convert the value in Watt to a position in px)
    assert(peak_demand_chart_y_range != 0);
    lv_coord_t pos = (lv_coord_t)((-1) * value * PEAK_DEMAND_CHART_HEIGHT_PX / peak_demand_chart_y_range);
    lv_obj_set_pos(max_peak_line, 0, pos);
    max_peak_line_pos_w = (lv_coord_t)value;

    // Update and realign the max peak label
    lv_label_set_text_fmt(max_peak_label, "%d W", value);
    lv_obj_align_to(max_peak_label, max_peak_line, LV_ALIGN_OUT_TOP_LEFT, 10, 0);
}

/**
 * @brief Set the new max peak demand
 *
 * When the peak demand chart is reset (at the start of a new quarter-hour), the new max peak demand is set to the current max peak demand.
 *
 * @param[in] value The new max peak demand in Watt
 */
void ui_set_new_max_peak_demand(uint16_t value) {
    if (value < MAX_PEAK_DEMAND_MIN_VALUE_W) {
        value = MAX_PEAK_DEMAND_MIN_VALUE_W;
    }
    new_max_peak_demand_w = value;
}

/**
 * @brief Set a data point in the peak demand chart
 *
 * @todo Make the alarm even more prominent when the peak demand is higher than the previous monthly maximum (e.g. beep every 15s instead of 30s)
 *
 * @param[in] time The time of the data point
 * @param[in] value The value of the data point in Watt
 */
void ui_add_peak_demand_data_point(time_t time, uint16_t value) {
    const uint8_t seconds_per_point = 900 / PEAK_DEMAND_CHART_POINT_COUNT;  // 900 seconds = 15 minutes
    struct tm * _tm = localtime(&time);

    // Calculate the number of seconds since the start of this quarter-hour
    uint16_t seconds =  (_tm->tm_min % 15) * 60 + _tm->tm_sec;

    // Check if we are at the start of a new quarter-hour
    if (seconds == 0 || value < peak_demand_last_point_w.y) {
        // Reset the chart data, so no old data is shown
        ui_reset_peak_demand_chart_data();
    }

    // Set the point in the chart at the calculated position
    set_peak_demand_chart_data_point(value, seconds / seconds_per_point);

    // Check if we missed any previous points, and if so, set them to the current value // TODO: interpolate them instead?
    for (int i = seconds / seconds_per_point - 1; i >= 0; i--) {
        lv_point_t point;
        lv_chart_get_point_pos_by_id(peak_demand_chart, peak_demand_chart_series, i, &point);
        if (point.y < 0) {  // points not set (LV_CHART_POINT_NONE) have a negative y value (under the x axis)
            set_peak_demand_chart_data_point(value, i);
        }
        else {
            break;
        }
    }
}

/**
 * @brief Reset the peak demand chart data
 *
 * Resets all the data points, the last point and sets the max peak line to the new max peak demand.
 */
void ui_reset_peak_demand_chart_data(void) {
    // Set all the values, so that they are not visible
    lv_chart_set_all_value(peak_demand_chart, peak_demand_chart_series, LV_CHART_POINT_NONE);

    // Reset the last point
    peak_demand_last_point_w.y = 0;
    peak_demand_last_point_w.x = 0;

    // Update the max peak line
    ui_set_max_peak_line(new_max_peak_demand_w);
}

/**
 * @brief Set the predicted peak of the current quarter-hour
 *
 * @param[in] value The predicted peak in Watt at the end of the quarter-hour
 */
void ui_set_predicted_peak(uint16_t value) {
    // Convert the last point (peak_demand_last_point_w) to pixels
    lv_chart_get_point_pos_by_id(peak_demand_chart, peak_demand_chart_series, peak_demand_last_point_w.x, &predicted_peak_line_points[0]);
    predicted_peak_line_points[0].x -= PEAK_DEMAND_CHART_PADDING_PX;

    // Convert the predicted point to pixels
    predicted_peak_line_points[1].y = (lv_coord_t)(PEAK_DEMAND_CHART_HEIGHT_PX - ((value * PEAK_DEMAND_CHART_HEIGHT_PX / peak_demand_chart_y_range)));
    predicted_peak_line_points[1].x = PEAK_DEMAND_CHART_POINT_COUNT - 1;

    // Redraw the line
    lv_line_set_points(predicted_peak_line, predicted_peak_line_points, 2);

    // Check if the predicted peak is higher than the current max peak and if we are not at the very start of the chart
    if (value > max_peak_line_pos_w && peak_demand_last_point_w.x >= 50) {
        // Start the alarm
        set_alarm_status(true);
    }
    else {
        // Stop the alarm
        set_alarm_status(false);
    }
}

/**
 * @brief Set the status of the WiFi connection
 *
 * @param[in] connected True if connected, false if not connected
 */
void ui_set_wifi_status(bool connected) {
    if (connected) {
        lv_img_set_src(wifi_symbol_img, &wifi_symbol_20_14);
    }
    else {
        lv_img_set_src(wifi_symbol_img, &wifi_symbol_nc_20_14);
    }
}

/**
 * @brief Set the status of the server connection
 *
 * @param[in] connected True if connected, false if not connected
 */
void ui_set_connected_status(bool connected) {
    if (connected) {
        lv_img_set_src(connected_symbol_img, &connected_symbol_14_14);
    }
    else {
        lv_img_set_src(connected_symbol_img, &connected_symbol_nc_14_14);
    }
}

/**
 * @brief Callback function for the 1s timer
 *
 * @param[in] timer
 */
static void timer_1s_cb(lv_timer_t * timer)
{
    // Update the time
    ui_set_time(ui_get_time());
}

/**
 * @brief Callback for the alarm timer
 *
 * This timer is used to blink the alarm style and beep every 30 seconds.
 *
 * @param timer
 */
static void alarm_timer_cb(lv_timer_t * timer)
{
    static bool blink = false;
    static uint8_t counter = 0;

    if (counter == 0) {
        ui_beep_long();
    }

    counter++;
    if (counter >= 60){
        counter = 0;
    }

    show_alarm_style(blink);
    blink = !blink;
}

/**
 * @brief Set a data point in the peak demand chart
 *
 * Set a data point in the peak demand chart at the given index to the given value.
 *
 * @param[in] value The value of the data point in Watt
 * @param[in] index The index of the data point (lower than PEAK_DEMAND_CHART_POINT_COUNT)
 */
static void set_peak_demand_chart_data_point(uint16_t value, uint8_t index) {
    assert(index < PEAK_DEMAND_CHART_POINT_COUNT);
    printf("Setting peak demand chart data point %d to %d\n", index, value);
    lv_chart_set_value_by_id(peak_demand_chart, peak_demand_chart_series, index, (lv_coord_t)value);
    peak_demand_last_point_w.y = (lv_coord_t)value;
    peak_demand_last_point_w.x = index;

    if (value > max_peak_line_pos_w) {
        ui_set_max_peak_line(max_peak_line_pos_w);
    }
}

/**
 * @brief Show or hide the alarm style
 *
 * This function changes the color of the power consumption label to red to indicate an alarm.
 *
 * @param alarm True to show the alarm style, false to hide it
 */
static void show_alarm_style(bool alarm) {
    if (alarm) {
        // Make the power consumption label red
        lv_obj_set_style_text_color(power_consumption_label, COLOR_KWARTIWI_RED, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(power_consumption_unit_label, COLOR_KWARTIWI_RED, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    else {
        // Reset the power consumption label color to the default
        lv_obj_set_style_text_color(power_consumption_label, TEXT_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(power_consumption_unit_label, TEXT_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/**
 * @brief Set the status of the alarm
 *
 * @param alarm True to start the alarm, false to stop it
 */
static void set_alarm_status(bool alarm) {
    if (alarm) {
        // Start the alarm timer
        lv_timer_resume(alarm_timer);
    }
    else {
        // Stop the alarm timer
        lv_timer_pause(alarm_timer);

        // Hide the alarm style
        show_alarm_style(false);
    }
}

/**
 * @brief Callback for the settings button
 * @param[in] e The event
 */
static void open_settings_event_cb(lv_event_t * e) {
    printf("Loading settings\n");
    // TODO: implement loading settings screen

    // load the touch calibration screen
    tp_call_load_screen();

}

/**
 * @brief Callback for the energy chart draw event
 *
 * @todo Implement so that that the x axis shows the time of the day
 *
 * @param[in] e The event
 */
static void energy_chart_draw_event_cb(lv_event_t * e) {
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_param(e);
    if(dsc->part == LV_PART_TICKS && dsc->id == LV_CHART_AXIS_PRIMARY_X) {
        const char * month[] = {"9u", "12u", "15u", "18u", "21u"};
        lv_snprintf(dsc->text, sizeof(dsc->text), "%s", month[dsc->value]);
    }
}

/**
 * @brief Callback for the peak demand chart draw event
 *
 * Draws the faded area below the line.
 * @author Code from an example in the LVGL documentation
 *
 * @param e
 */
static void peak_demand_chart_draw_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);

    /*Add the faded area before the lines are drawn*/
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    if(dsc->part == LV_PART_ITEMS) {
        if(!dsc->p1 || !dsc->p2) return;

        /*Add a line mask that keeps the area below the line*/
        lv_draw_mask_line_param_t line_mask_param;
        lv_draw_mask_line_points_init(&line_mask_param, dsc->p1->x, dsc->p1->y, dsc->p2->x, dsc->p2->y, LV_DRAW_MASK_LINE_SIDE_BOTTOM);
        int16_t line_mask_id = lv_draw_mask_add(&line_mask_param, NULL);

        /*Add a fade effect: transparent bottom covering top*/
        lv_coord_t h = lv_obj_get_height(obj);
        lv_draw_mask_fade_param_t fade_mask_param;
        lv_draw_mask_fade_init(&fade_mask_param, &obj->coords, LV_OPA_COVER, (lv_coord_t)(obj->coords.y1 + h / 4), LV_OPA_TRANSP, obj->coords.y2);
        int16_t fade_mask_id = lv_draw_mask_add(&fade_mask_param, NULL);

        /*Draw a rectangle that will be affected by the mask*/
        lv_draw_rect_dsc_t draw_rect_dsc;
        lv_draw_rect_dsc_init(&draw_rect_dsc);
        draw_rect_dsc.bg_opa = LV_OPA_20;
        draw_rect_dsc.bg_color = dsc->line_dsc->color;

        lv_area_t a;
        a.x1 = dsc->p1->x;
        a.x2 = (lv_coord_t)(dsc->p2->x - 1);
        a.y1 = LV_MIN(dsc->p1->y, dsc->p2->y);
        a.y2 = obj->coords.y2;
        lv_draw_rect(dsc->draw_ctx, &draw_rect_dsc, &a);

        /*Remove the masks*/
        lv_draw_mask_free_param(&line_mask_param);
        lv_draw_mask_free_param(&fade_mask_param);
        lv_draw_mask_remove_id(line_mask_id);
        lv_draw_mask_remove_id(fade_mask_id);
    }
}