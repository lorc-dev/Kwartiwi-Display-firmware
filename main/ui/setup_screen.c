/**
 * @file setup_screen.c
 *
 * @brief The setup screen is used to guide the user through the process of connecting
 * to a Wi-Fi network and to a KWARTIWI P1 server.
 * When the setup is done, the device will reboot.
 *
 * @todo Implement the reload button for the Wi-Fi network and the server list
 * @todo Improve the "Wrapping up" screen (showing the user that the configuration is being saved and the device is rebooting)
 * @todo Use better icons for the welcome and wrapping up screen
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "lvgl.h"
#include "ui.h"
#include "setup_screen.h"

#define TITLE_TEXT "Setup your KWARTIWI"
#define WELCOME_TEXT "This setup will guide you through the process of connecting your KWARTIWI Display to your Wi-Fi network and to your KWARTIWI P1."
#define INSTRUCTION_WELCOME_TEXT "Welcome to KWARTIWI!"
#define INSTRUCTION_SSID_SELECT_TEXT "Select your network"
#define INSTRUCTION_WIFI_PASSPHRASE_TEXT "Type your Wi-Fi credentials"
#define INSTRUCTION_SERVER_SELECT_TEXT "Select your KWARTIWI P1"
#define INSTRUCTION_WRAPPING_UP_TEXT "Wrapping up"

// Fonts and images
LV_FONT_DECLARE(roboto_bold_70);
LV_IMG_DECLARE(wifi_symbol_orange_50_50);
LV_IMG_DECLARE(server_symbol_orange_50_50);
LV_IMG_DECLARE(reload_symbol_blue_20_20);

typedef enum {
    STATE_WELCOME,
    STATE_SELECT_NETWORK,
    STATE_NETWORK_PASS,
    STATE_NETWORK_CONNECT,
    STATE_SERVER_SELECT,
    STATE_WRAPPING_UP,

    STATE_COUNT,
} setup_state_t;

// The config struct is used to temporarily store the user's input
static struct config_s {
    char ssid[32];
    char passphrase[64];
    char server[255];
} config;

bool setup_screen_initialized = false;

// LVGL objects
lv_obj_t * setup_screen;
static lv_obj_t * title_label;
static lv_obj_t * progress_bar;
static lv_obj_t * instruction_icon_img;
static lv_obj_t * instruction_label;
static lv_obj_t * reload_symbol_img;
static lv_obj_t * next_btn;
static lv_obj_t * next_label;
static lv_obj_t * prev_btn;
static lv_obj_t * prev_label;
static lv_obj_t * selection_roller;
static lv_obj_t * wifi_network_pass_text_area;
static lv_obj_t * keyboard;
static lv_obj_t * network_name_label;
static lv_obj_t * loading_spinner;
static lv_obj_t * status_label;
static lv_obj_t * welcome_label;

// The current state of the setup screen
static setup_state_t current_state = STATE_WELCOME;

// Function prototypes
static void show_state(setup_state_t state);
static void navigation_event_cb(lv_event_t * event);
static void passphrase_event_cb(lv_event_t * event);
static void select_network_finished(void);
static void network_pass_finished(void);
static void server_select_finished(void);
static void save_config(void);

/**
 * @brief Initialize the setup screen
 */
void setup_screen_init(void)
{
    setup_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(setup_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(setup_screen, COLOR_WHITE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(setup_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Title
    title_label = lv_label_create(setup_screen);
    lv_obj_set_width(title_label, LV_SIZE_CONTENT);
    lv_obj_set_height(title_label, LV_SIZE_CONTENT);
    lv_obj_set_align(title_label, LV_ALIGN_TOP_MID);
    lv_obj_set_x(title_label, 0);
    lv_obj_set_y(title_label, 5);
    lv_label_set_text(title_label, TITLE_TEXT);

    // Progress bar
    progress_bar = lv_bar_create(setup_screen);
    lv_obj_set_width(progress_bar, LV_HOR_RES - 10);
    lv_obj_set_height(progress_bar, 10);
    lv_obj_set_align(progress_bar, LV_ALIGN_TOP_MID);
    lv_obj_set_x(progress_bar, 0);
    lv_obj_set_y(progress_bar, 25);
    lv_obj_set_style_bg_color(progress_bar, COLOR_KWARTIWI_GREY, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(progress_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(progress_bar, COLOR_KWARTIWI_ORANGE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(progress_bar, COLOR_KWARTIWI_RED , LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(progress_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_bar_set_value(progress_bar, 100, LV_ANIM_OFF);

    // Icon
    instruction_icon_img = lv_img_create(setup_screen);
    lv_obj_set_width(instruction_icon_img, LV_SIZE_CONTENT);
    lv_obj_set_height(instruction_icon_img, LV_SIZE_CONTENT);
    lv_obj_set_align(instruction_icon_img, LV_ALIGN_TOP_MID);
    lv_obj_set_x(instruction_icon_img, 0);
    lv_obj_set_y(instruction_icon_img, 50);
    lv_obj_clear_flag(instruction_icon_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_img_set_src(instruction_icon_img, &wifi_symbol_orange_50_50);

    // Instruction text
    instruction_label = lv_label_create(setup_screen);
    lv_obj_set_width(instruction_label, LV_SIZE_CONTENT);
    lv_obj_set_height(instruction_label, LV_SIZE_CONTENT);
    lv_obj_set_align(instruction_label, LV_ALIGN_CENTER);
    lv_obj_set_x(instruction_label, 0);
    lv_obj_set_y(instruction_label, -50);
    lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(instruction_label, INSTRUCTION_SSID_SELECT_TEXT);

    // Reload icon
    reload_symbol_img = lv_img_create(setup_screen);
    lv_img_set_src(reload_symbol_img, &reload_symbol_blue_20_20);
    lv_obj_set_width(reload_symbol_img, LV_SIZE_CONTENT);
    lv_obj_set_height(reload_symbol_img, LV_SIZE_CONTENT);
    lv_obj_set_align(reload_symbol_img, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_x(reload_symbol_img, 0);
    lv_obj_set_y(reload_symbol_img, -15);
    lv_obj_clear_flag(reload_symbol_img, LV_OBJ_FLAG_SCROLLABLE);

    // Next button
    next_btn = lv_btn_create(setup_screen);
    lv_obj_set_width(next_btn, 30);
    lv_obj_set_height(next_btn, 30);
    lv_obj_set_align(next_btn, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_x(next_btn, -10);
    lv_obj_set_y(next_btn, -10);
    lv_obj_clear_flag(next_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(next_btn, 1, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(next_btn, navigation_event_cb, LV_EVENT_CLICKED, NULL);

    // Next label
    next_label = lv_label_create(next_btn);
    lv_obj_set_width(next_label, LV_SIZE_CONTENT);
    lv_obj_set_height(next_label, LV_SIZE_CONTENT);
    lv_obj_set_align(next_label, LV_ALIGN_CENTER);
    lv_label_set_text(next_label, ">");
    lv_obj_set_style_text_color(next_label, COLOR_KWARTIWI_ORANGE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(next_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Prev button
    prev_btn = lv_btn_create(setup_screen);
    lv_obj_set_width(prev_btn, 30);
    lv_obj_set_height(prev_btn, 30);
    lv_obj_set_align(prev_btn, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_x(prev_btn, 10);
    lv_obj_set_y(prev_btn, -10);
    lv_obj_clear_flag(prev_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(prev_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(prev_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(prev_btn, 1, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(prev_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(prev_btn, navigation_event_cb, LV_EVENT_CLICKED, NULL);

    // Prev label
    prev_label = lv_label_create(prev_btn);
    lv_obj_set_width(prev_label, LV_SIZE_CONTENT);
    lv_obj_set_height(prev_label, LV_SIZE_CONTENT);
    lv_obj_set_align(prev_label, LV_ALIGN_CENTER);
    lv_label_set_text(prev_label, "<");
    lv_obj_set_style_text_color(prev_label, COLOR_KWARTIWI_ORANGE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(prev_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Wi-Fi network selector
    selection_roller = lv_roller_create(setup_screen);
    lv_roller_set_options(selection_roller, "", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(selection_roller, 200);
    lv_obj_set_height(selection_roller, 140);
    lv_obj_set_align(selection_roller, LV_ALIGN_CENTER);
    lv_obj_set_x(selection_roller, 0);
    lv_obj_set_y(selection_roller, 40);
    lv_obj_set_style_bg_color(selection_roller, COLOR_KWARTIWI_BLUE, LV_PART_SELECTED | LV_STATE_DEFAULT);

    // Wi-Fi network password text area
    wifi_network_pass_text_area = lv_textarea_create(setup_screen);
    lv_obj_set_width(wifi_network_pass_text_area, LV_HOR_RES - 10);
    lv_obj_set_height(wifi_network_pass_text_area, LV_SIZE_CONTENT);
    lv_obj_set_align(wifi_network_pass_text_area, LV_ALIGN_CENTER);
    lv_obj_set_x(wifi_network_pass_text_area, 0);
    lv_obj_set_y(wifi_network_pass_text_area, -5);
    lv_textarea_set_placeholder_text(wifi_network_pass_text_area, "Passphrase...");
    lv_textarea_set_one_line(wifi_network_pass_text_area, true);
    lv_textarea_set_max_length(wifi_network_pass_text_area, 63);
    lv_obj_add_flag(wifi_network_pass_text_area, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(wifi_network_pass_text_area, passphrase_event_cb, LV_EVENT_ALL, NULL);

    // Wi-Fi network name label
    network_name_label = lv_label_create(setup_screen);
    lv_obj_set_width(network_name_label, LV_SIZE_CONTENT);
    lv_obj_set_height(network_name_label, LV_SIZE_CONTENT);
    lv_obj_set_align(network_name_label, LV_ALIGN_CENTER);
    lv_obj_align_to(network_name_label, wifi_network_pass_text_area, LV_ALIGN_OUT_TOP_MID, 0, -10);
    lv_obj_set_style_text_color(network_name_label, COLOR_KWARTIWI_BLUE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(network_name_label, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(network_name_label, "Network name");

    // Keyboard
    keyboard = lv_keyboard_create(setup_screen);
    lv_obj_set_width(keyboard, 240);
    lv_obj_set_height(keyboard, 120);
    lv_obj_set_align(keyboard, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_x(keyboard, 0);
    lv_obj_set_y(keyboard, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(keyboard, wifi_network_pass_text_area);

    // Loading spinner
    loading_spinner = lv_spinner_create(setup_screen, 1500, 45);
    lv_obj_set_width(loading_spinner, 100);
    lv_obj_set_height(loading_spinner, 100);
    lv_obj_set_align(loading_spinner, LV_ALIGN_CENTER);
    lv_obj_set_x(loading_spinner, 0);
    lv_obj_set_y(loading_spinner, 40);

    // Status label
    status_label = lv_label_create(setup_screen);
    lv_obj_set_width(status_label, LV_SIZE_CONTENT);
    lv_obj_set_height(status_label, LV_SIZE_CONTENT);
    lv_obj_set_align(status_label, LV_ALIGN_CENTER);
    lv_label_set_text(status_label, "");

    // Welcome label
    welcome_label = lv_label_create(setup_screen);
    lv_obj_set_width(welcome_label, 220);
//    lv_obj_set_height(welcome_label, LV_SIZE_CONTENT);
    lv_obj_align(welcome_label, LV_ALIGN_CENTER , 0, 30);
    lv_label_set_long_mode(welcome_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(welcome_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(welcome_label, WELCOME_TEXT);

    // Show the welcome screen
    show_state(current_state);

    setup_screen_initialized = true;
}

/**
 * @brief Hide all UI elements that change between different states
 */
static void hide_everything(void) {
    lv_obj_add_flag(selection_roller, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(reload_symbol_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_network_pass_text_area, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(network_name_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(welcome_label, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Show the UI elements for a given state and execute any state-specific actions
 *
 * @param[in] state The state to show
 */
static void show_state(const setup_state_t state) {
    hide_everything();

    // Hide the prev/next buttons if we're at the beginning or end of the setup process
    if (current_state <= STATE_WELCOME) {
        lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else if (current_state >= STATE_COUNT - 1) {
        lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
    }

    // Show the UI elements for the given state
    switch (state) {
        case STATE_WELCOME:
            lv_label_set_text(instruction_label, INSTRUCTION_WELCOME_TEXT);
            lv_img_set_src(instruction_icon_img, &wifi_symbol_orange_50_50); // TODO: change icon to a setup symbol or the kwartiwi logo
            lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
            lv_obj_clear_flag(welcome_label, LV_OBJ_FLAG_HIDDEN);
            break;
        case STATE_SELECT_NETWORK:
            lv_label_set_text(instruction_label, INSTRUCTION_SSID_SELECT_TEXT);
            lv_img_set_src(instruction_icon_img, &wifi_symbol_orange_50_50);
            lv_bar_set_value(progress_bar, 10, LV_ANIM_OFF);
            lv_obj_clear_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
            ui_scan_wifi_networks();
            lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
            break;
        case STATE_NETWORK_PASS:
            lv_obj_clear_flag(wifi_network_pass_text_area, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(network_name_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(instruction_label, INSTRUCTION_WIFI_PASSPHRASE_TEXT);
            lv_bar_set_value(progress_bar, 30, LV_ANIM_OFF);
            lv_obj_set_y(wifi_network_pass_text_area, 30);
            lv_obj_align_to(network_name_label, wifi_network_pass_text_area, LV_ALIGN_OUT_TOP_MID, 0, -10);
            lv_label_set_text(network_name_label, config.ssid);
            lv_obj_set_y(status_label, 60);
            break;
        case STATE_NETWORK_CONNECT:
            lv_obj_clear_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(instruction_label, "Connecting...");
            lv_img_set_src(instruction_icon_img, &wifi_symbol_orange_50_50);
            lv_bar_set_value(progress_bar, 60, LV_ANIM_OFF);
            lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
            break;
        case STATE_SERVER_SELECT:
            // TODO: populate the roller with the discovered MQTT brokers
            lv_label_set_text(instruction_label, INSTRUCTION_SERVER_SELECT_TEXT);
            lv_bar_set_value(progress_bar, 80, LV_ANIM_OFF);
            lv_obj_clear_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(instruction_icon_img, &server_symbol_orange_50_50);
            lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
            ui_find_servers();
            break;

        case STATE_WRAPPING_UP:
            lv_label_set_text(instruction_label, INSTRUCTION_WRAPPING_UP_TEXT);
            lv_bar_set_value(progress_bar, 100, LV_ANIM_OFF);
            lv_img_set_src(instruction_icon_img, &wifi_symbol_orange_50_50);
            lv_obj_clear_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
            save_config();
            break;
        case STATE_COUNT:
            break;
    }

}

/**
 * @brief Callback for the prev/next buttons
 *
 * Performs the appropriate action for the current state and advances to the next or previous state
 *
 * @param[in] event
 */
static void navigation_event_cb(lv_event_t * event) {
    lv_obj_t * target = lv_event_get_current_target(event);
    if (target == next_btn) {
        switch (current_state) {
            case STATE_WELCOME:
                current_state = STATE_SELECT_NETWORK;
                break;
            case STATE_SELECT_NETWORK:
                select_network_finished();
                current_state = STATE_NETWORK_PASS;
                break;
            case STATE_NETWORK_PASS:
                network_pass_finished();
                current_state = STATE_NETWORK_CONNECT;
                break;
            case STATE_NETWORK_CONNECT:
                current_state = STATE_SERVER_SELECT;
                break;
            case STATE_SERVER_SELECT:
                server_select_finished();
                current_state = STATE_WRAPPING_UP;
                break;
            case STATE_WRAPPING_UP:
            case STATE_COUNT:
                // Should never get here
                abort();
                break;
        }
    }
    if (target == prev_btn) {
        switch (current_state) {
            case STATE_WELCOME:
                // Should never get here
                abort();
                break;
            case STATE_SELECT_NETWORK:
                current_state = STATE_WELCOME;
                break;
            case STATE_NETWORK_PASS:
                current_state = STATE_SELECT_NETWORK;
                break;
            case STATE_NETWORK_CONNECT:
                current_state = STATE_NETWORK_PASS;
                break;
            case STATE_SERVER_SELECT:
                current_state = STATE_SELECT_NETWORK;
                break;
            case STATE_WRAPPING_UP:
                current_state = STATE_SERVER_SELECT;
                break;
            case STATE_COUNT:
                // Should never get here
                abort();
                break;
        }
    }

    // Show the new state
    show_state(current_state);
}

/**
 * @brief Callback for the wifi password text area
 *
 * Shows the keyboard when the text area is clicked, and hides it when the text area is ready (ok button on keyboard pressed)
 *
 * @param[in] event
 */
static void passphrase_event_cb(lv_event_t * event) {
    if (event->code == LV_EVENT_READY ) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(wifi_network_pass_text_area, 30);
        lv_obj_align_to(network_name_label, wifi_network_pass_text_area, LV_ALIGN_OUT_TOP_MID, 0, -10);
    }

    if (event->code == LV_EVENT_CLICKED) {
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(wifi_network_pass_text_area, 0);
        lv_obj_align_to(network_name_label, wifi_network_pass_text_area, LV_ALIGN_OUT_TOP_MID, 0, -10);
        lv_label_set_text(status_label, "");
    }
}

/**
 * @brief Called when the network selection step is finished
 *
 * Copies the selected network name into the config struct
 */
static void select_network_finished(void) {
    lv_roller_get_selected_str(selection_roller, config.ssid, sizeof(config.ssid));
}

/**
 * @brief Called when the network password step is finished
 *
 * Copies the entered password into the config struct, and attempts to connect to the network
 */
static void network_pass_finished(void) {
    const char * pass = lv_textarea_get_text(wifi_network_pass_text_area);
    strncpy(config.passphrase, pass, sizeof(config.passphrase) - 1);

    ui_set_wifi_config(config.ssid, config.passphrase);
    ui_connect_wifi();
}

/**
 * @brief Called when the server selection step is finished
 *
 * Copies the selected server name into the config struct
 */
static void server_select_finished(void) {
    lv_roller_get_selected_str(selection_roller, config.server, sizeof(config.server));
}

/**
 * @brief Saves the config and reboots the device
 */
static void save_config(void) {
    // Save config
    ui_save_config(config.ssid, config.passphrase, config.server);

    // Mark setup as done
    ui_set_setup_done(true);

    // Reboot
    ui_reboot();
}

/**
 * @brief Callback for the wifi connected event
 *
 * @param[in] connected True if connected, false if disconnected
 */
void setup_screen_wifi_connected_status_cb(const bool connected) {
    if (current_state != STATE_NETWORK_CONNECT) {
        // TODO: If we're in a state after the connection is made, notify the user that the connection failed
        return;
    }

    if (connected) {
        // Move to the next state
        current_state = STATE_SERVER_SELECT;
        show_state(current_state);
    }
    else {
        // Go back to the previous state, and show an error
        current_state = STATE_NETWORK_PASS;
        show_state(current_state);
        lv_label_set_text(status_label, "Failed to connect to network!");
        lv_obj_set_style_text_color(status_label, COLOR_KWARTIWI_RED, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // Show the buttons again
    lv_obj_clear_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Callback for the wifi scan result event
 *
 * @param[in] networks The list of available networks
 * @param[in] len The number of networks in the list
 */
void setup_screen_set_wifi_scan_result(const ui_wifi_network_t *const networks, const uint16_t len) {
    // Create a string with all the network names separated by newlines for the roller
    char options_str[32 * len];
    uint8_t options_str_len = 0;
    for (uint16_t i = 0; i < len; i++) {
        options_str_len += sprintf(options_str + options_str_len, "%s\n", networks[i].ssid);
    }
    options_str[--options_str_len] = '\0';

    // Set the options for the roller
    lv_roller_set_options(selection_roller, options_str, LV_ROLLER_MODE_NORMAL);

    // Hide and show the correct elements
    lv_obj_add_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(selection_roller, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reload_symbol_img, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(progress_bar, 20, LV_ANIM_OFF);

    // Show the navigation buttons again
    lv_obj_clear_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Callback for the server scan result event
 *
 * @param[in] servers The list of available servers
 * @param[in] len The number of servers in the list
 */
void setup_screen_set_servers_found(const ui_server_t *const servers, const uint8_t len) {
    // Create a string with all the server names separated by newlines for the roller
    char options_str[256 * len];
    uint8_t options_str_len = 0;
    for (uint16_t i = 0; i < len; i++) {
        options_str_len += sprintf(options_str + options_str_len, "%s\n", servers[i].hostname);
    }
    options_str[--options_str_len] = '\0';

    // Set the options for the roller
    lv_roller_set_options(selection_roller, options_str, LV_ROLLER_MODE_NORMAL);

    // Hide and show the correct elements
    lv_obj_add_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(selection_roller, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reload_symbol_img, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(progress_bar, 80, LV_ANIM_OFF);

    // Show the navigation buttons again
    lv_obj_clear_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
}