/*
 * Smart Posture Receiver (ESP32-S3-BOX-3)
 * Mode: Offline / Channel 1 Fixed
 * Update: High-Visibility Water Timer (White) & Alert (Orange)
 */

#include <stdio.h>
#include <string.h>
#include <math.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

// --- Colors ---
#define COLOR_BG          lv_color_hex(0x02050A) 
#define COLOR_CARD_TOP    lv_color_hex(0x1A2633) 
#define COLOR_CARD_BOT    lv_color_hex(0x0F1926) 
#define COLOR_CYAN        lv_color_hex(0x00E5FF) 
#define COLOR_LIGHT_BLUE  lv_color_hex(0x64B5F6) 
#define COLOR_GREEN       lv_color_hex(0x00E676) 
#define COLOR_RED         lv_color_hex(0xFF5252) 
#define COLOR_ORANGE      lv_color_hex(0xFFAB40)
#define COLOR_TEXT_GRAY   lv_color_hex(0x90A4AE) 
#define COLOR_TANK_BG     lv_color_hex(0x0A121E)

// --- PACKETS ---
typedef struct {
    float pitch;
    float roll;
    int battery_level;
} posture_packet_t;

typedef struct {
    uint8_t command_id; 
    uint8_t value;      
} command_packet_t;

uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// --- GLOBAL STATE ---
static int water_count = 0;           
static QueueHandle_t posture_queue;
static uint32_t last_packet_tick = 0; 
#define CONNECTION_TIMEOUT_MS 3000

static float current_pitch = 0;

// --- WATER REMINDER VARS ---
static int water_timer_ticks = 0;
// 30ms per tick. 120,000 ticks = 60 mins.
#define WATER_REMINDER_THRESHOLD  120000 
static bool water_alert_active = false;

// --- UI Objects ---
static lv_obj_t *scr;
static lv_obj_t *panel_home, *panel_stats, *panel_settings;
static lv_obj_t *nav_labels[3]; 
static lv_obj_t *label_wifi_icon;
static lv_obj_t *label_posture_status; // Header
static lv_obj_t *spine_track, *posture_dot;
static lv_obj_t *label_pitch_val;
static lv_obj_t *water_bar, *label_water_pct, *label_water_timer;
static lv_obj_t *chart_posture;
static lv_chart_series_t *ser_posture;
static lv_obj_t *sw_vibration, *sw_wifi, *lbl_wifi_status;
static lv_obj_t *btn_cal, *lbl_cal; 

// ======================= ESP-NOW LOGIC =======================

static void on_data_recv(const esp_now_recv_info_t * info, const uint8_t * incomingData, int len) {
    if (len == sizeof(posture_packet_t)) {
        posture_packet_t packet;
        memcpy(&packet, incomingData, sizeof(packet));
        xQueueOverwrite(posture_queue, &packet);
    }
}

void send_calibration_command() {
    command_packet_t cmd;
    cmd.command_id = 1; 
    cmd.value = 0;
    esp_now_send(BROADCAST_MAC, (uint8_t *)&cmd, sizeof(cmd));
}

void send_vibration_setting(bool enabled) {
    command_packet_t cmd;
    cmd.command_id = 2; 
    cmd.value = enabled ? 1 : 0; 
    esp_now_send(BROADCAST_MAC, (uint8_t *)&cmd, sizeof(cmd));
}

static void init_esp_now(void) {
    posture_queue = xQueueCreate(1, sizeof(posture_packet_t));
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv));
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, BROADCAST_MAC, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void wifi_init_offline(void) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

// ======================= HELPERS =======================

static void opa_anim_cb(void * obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static lv_obj_t * create_glass_card(lv_obj_t * parent, int w, int h) {
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, COLOR_CARD_TOP, 0);
    lv_obj_set_style_bg_grad_color(obj, COLOR_CARD_BOT, 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(obj, lv_color_white(), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_20, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void update_water_ui(void) {
    int pct = (water_count * 100) / 8;
    lv_bar_set_value(water_bar, pct, LV_ANIM_ON);
    lv_label_set_text_fmt(label_water_pct, "%d / 8", water_count);
}

static void switch_tab(int tab_id) {
    lv_obj_t * tabs[] = {panel_home, panel_stats, panel_settings};
    for(int i=0; i<3; i++) {
        if(i == tab_id) {
            lv_obj_clear_flag(tabs[i], LV_OBJ_FLAG_HIDDEN);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, tabs[i]);
            lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_time(&a, 300);
            lv_anim_set_exec_cb(&a, opa_anim_cb); 
            lv_anim_start(&a);
            lv_obj_set_style_text_color(nav_labels[i], COLOR_CYAN, 0);
        } else {
            lv_obj_add_flag(tabs[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(nav_labels[i], COLOR_TEXT_GRAY, 0);
        }
    }
}

// ======================= CALLBACKS =======================

static void nav_click_cb(lv_event_t * e) {
    switch_tab((int)(size_t)lv_event_get_user_data(e));
}

static void btn_water_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_SHORT_CLICKED) {
        if (water_count < 8) { water_count++; }
        water_timer_ticks = 0; 
        water_alert_active = false;
        update_water_ui();
    } else if (code == LV_EVENT_LONG_PRESSED) {
        water_count = 0; update_water_ui();
    }
}

static void cal_reset_timer_cb(lv_timer_t * t) {
    lv_label_set_text(lbl_cal, LV_SYMBOL_REFRESH " CALIBRATE");
    lv_obj_set_style_text_color(lbl_cal, COLOR_CYAN, 0);
    lv_obj_clear_state(btn_cal, LV_STATE_DISABLED);
}

static void btn_calibrate_cb(lv_event_t * e) {
    send_calibration_command();
    lv_label_set_text(lbl_cal, "HOLD STILL...");
    lv_obj_set_style_text_color(lbl_cal, COLOR_ORANGE, 0);
    lv_obj_add_state(btn_cal, LV_STATE_DISABLED);
    lv_timer_create(cal_reset_timer_cb, 3000, NULL);
}

static void toggle_vibration_cb(lv_event_t * e) {
    bool state = lv_obj_has_state(sw_vibration, LV_STATE_CHECKED);
    send_vibration_setting(state);
}

static void toggle_wifi_cb(lv_event_t * e) {
    bool state = lv_obj_has_state(sw_wifi, LV_STATE_CHECKED);
    if(state) {
        lv_label_set_text(lbl_wifi_status, "Offline Mode (Ch 1)");
        esp_wifi_start();
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    } else {
        esp_wifi_stop();
        lv_label_set_text(lbl_wifi_status, "Radio Off");
        lv_obj_set_style_text_color(lbl_wifi_status, COLOR_TEXT_GRAY, 0);
        lv_obj_set_style_text_color(label_wifi_icon, COLOR_TEXT_GRAY, 0);
    }
}

// ======================= UI BUILDERS =======================

void build_home_tab(void) {
    panel_home = lv_obj_create(scr);
    lv_obj_set_size(panel_home, 320, 195);
    lv_obj_align(panel_home, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_style_bg_opa(panel_home, 0, 0);
    lv_obj_set_style_border_width(panel_home, 0, 0);

    // --- LEFT CARD ---
    lv_obj_t * p_left = create_glass_card(panel_home, 180, 165);
    lv_obj_align(p_left, LV_ALIGN_TOP_LEFT, 5, 0);

    label_pitch_val = lv_label_create(p_left);
    lv_obj_set_style_text_color(label_pitch_val, COLOR_TEXT_GRAY, 0);
    lv_obj_set_style_text_font(label_pitch_val, &lv_font_montserrat_12, 0);
    lv_obj_align(label_pitch_val, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_label_set_text(label_pitch_val, "P: --");

    spine_track = lv_obj_create(p_left);
    lv_obj_set_size(spine_track, 4, 100); 
    lv_obj_set_style_bg_color(spine_track, COLOR_TEXT_GRAY, 0);
    lv_obj_set_style_bg_opa(spine_track, LV_OPA_30, 0);
    lv_obj_set_style_border_width(spine_track, 0, 0);
    lv_obj_set_style_radius(spine_track, 2, 0);
    lv_obj_align(spine_track, LV_ALIGN_CENTER, 0, -15); 

    lv_obj_t * dash = lv_obj_create(p_left);
    lv_obj_set_size(dash, 20, 2);
    lv_obj_set_style_bg_color(dash, COLOR_TEXT_GRAY, 0);
    lv_obj_set_style_bg_opa(dash, LV_OPA_50, 0);
    lv_obj_set_style_border_width(dash, 0, 0);
    lv_obj_align(dash, LV_ALIGN_CENTER, 0, -15); 

    posture_dot = lv_obj_create(p_left);
    lv_obj_set_size(posture_dot, 20, 20); 
    lv_obj_set_style_radius(posture_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(posture_dot, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(posture_dot, 2, 0);
    lv_obj_set_style_border_color(posture_dot, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(posture_dot, 10, 0);
    lv_obj_set_style_shadow_color(posture_dot, COLOR_CYAN, 0);
    lv_obj_align_to(posture_dot, spine_track, LV_ALIGN_CENTER, 0, 0);

    btn_cal = lv_btn_create(p_left);
    lv_obj_set_size(btn_cal, 140, 30);
    lv_obj_align(btn_cal, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(btn_cal, LV_OPA_TRANSP, 0);      
    lv_obj_set_style_shadow_width(btn_cal, 0, 0);            
    lv_obj_set_style_border_width(btn_cal, 0, 0);            
    lv_obj_add_event_cb(btn_cal, btn_calibrate_cb, LV_EVENT_CLICKED, NULL);

    lbl_cal = lv_label_create(btn_cal);
    lv_label_set_text(lbl_cal, LV_SYMBOL_REFRESH " CALIBRATE");
    lv_obj_set_style_text_font(lbl_cal, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_cal, COLOR_CYAN, 0);
    lv_obj_center(lbl_cal);

    // --- RIGHT CARD (Water) ---
    lv_obj_t * p_right = create_glass_card(panel_home, 85, 165);
    lv_obj_align(p_right, LV_ALIGN_TOP_RIGHT, -5, 0);

    lv_obj_t * lbl_title = lv_label_create(p_right);
    lv_label_set_text(lbl_title, "WATER");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_12, 0); 
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_GRAY, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 5);

    water_bar = lv_bar_create(p_right);
    lv_obj_set_size(water_bar, 60, 90); 
    lv_obj_align(water_bar, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_style_bg_color(water_bar, COLOR_TANK_BG, LV_PART_MAIN); 
    lv_obj_set_style_radius(water_bar, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(water_bar, COLOR_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(water_bar, COLOR_LIGHT_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(water_bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(water_bar, 12, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(water_bar, 1000, 0);

    lv_obj_t * icon_drop = lv_label_create(p_right);
    lv_label_set_text(icon_drop, LV_SYMBOL_TINT);
    lv_obj_set_style_text_color(icon_drop, lv_color_white(), 0);
    lv_obj_align_to(icon_drop, water_bar, LV_ALIGN_TOP_MID, 0, 15);

    label_water_pct = lv_label_create(p_right);
    lv_obj_set_style_text_font(label_water_pct, &lv_font_montserrat_20, 0); 
    lv_obj_set_style_text_color(label_water_pct, lv_color_white(), 0);
    lv_obj_align_to(label_water_pct, water_bar, LV_ALIGN_CENTER, 0, 5);

    // --- FIX: VISIBLE WATER TIMER ---
    label_water_timer = lv_label_create(p_right);
    lv_label_set_text(label_water_timer, "60:00");
    // Changed font to 14 and Color to White for better visibility
    lv_obj_set_style_text_font(label_water_timer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_water_timer, lv_color_white(), 0);
    lv_obj_align_to(label_water_timer, water_bar, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_obj_t * btn_add = lv_btn_create(p_right);
    lv_obj_set_size(btn_add, 50, 40); 
    lv_obj_align(btn_add, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(btn_add, LV_OPA_TRANSP, 0); 
    lv_obj_add_event_cb(btn_add, btn_water_cb, LV_EVENT_ALL, NULL); 
    
    lv_obj_t * lbl_add = lv_label_create(btn_add);
    lv_label_set_text(lbl_add, "+");
    lv_obj_set_style_text_font(lbl_add, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_add, COLOR_CYAN, 0);
    lv_obj_center(lbl_add);

    update_water_ui();
}

void build_stats_tab(void) {
    panel_stats = lv_obj_create(scr);
    lv_obj_set_size(panel_stats, 320, 195);
    lv_obj_align(panel_stats, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_style_bg_opa(panel_stats, 0, 0);
    lv_obj_set_style_border_width(panel_stats, 0, 0);
    lv_obj_add_flag(panel_stats, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * card = create_glass_card(panel_stats, 280, 165);
    lv_obj_center(card);

    lv_obj_t * title = lv_label_create(card);
    lv_label_set_text(title, "LAST 1 HOUR (Posture Score)");
    lv_obj_set_style_text_color(title, COLOR_TEXT_GRAY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 5);

    chart_posture = lv_chart_create(card);
    lv_obj_set_size(chart_posture, 260, 120);
    lv_obj_align(chart_posture, LV_ALIGN_CENTER, 0, 10);
    lv_chart_set_type(chart_posture, LV_CHART_TYPE_LINE); 
    
    lv_chart_set_range(chart_posture, LV_CHART_AXIS_PRIMARY_Y, 0, 60);
    lv_chart_set_point_count(chart_posture, 60);
    
    lv_obj_set_style_bg_opa(chart_posture, 0, 0); 
    lv_obj_set_style_border_width(chart_posture, 0, 0);
    lv_obj_set_style_line_width(chart_posture, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_posture, 0, 0, LV_PART_INDICATOR); 

    ser_posture = lv_chart_add_series(chart_posture, COLOR_CYAN, LV_CHART_AXIS_PRIMARY_Y);
    
    for(int i=0; i<60; i++) {
        lv_chart_set_next_value(chart_posture, ser_posture, 0);
    }
}

void build_settings_tab(void) {
    panel_settings = lv_obj_create(scr);
    lv_obj_set_size(panel_settings, 320, 195);
    lv_obj_align(panel_settings, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_style_bg_opa(panel_settings, 0, 0);
    lv_obj_set_style_border_width(panel_settings, 0, 0);
    lv_obj_add_flag(panel_settings, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * card = create_glass_card(panel_settings, 280, 165);
    lv_obj_center(card);

    // Wi-Fi
    lv_obj_t * lbl_wifi = lv_label_create(card);
    lv_label_set_text(lbl_wifi, "Offline Link");
    lv_obj_set_style_text_color(lbl_wifi, lv_color_white(), 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_TOP_LEFT, 20, 15);
    sw_wifi = lv_switch_create(card);
    lv_obj_align(sw_wifi, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_add_state(sw_wifi, LV_STATE_CHECKED); 
    lv_obj_set_style_bg_color(sw_wifi, COLOR_CYAN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_wifi, toggle_wifi_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_wifi_status = lv_label_create(card);
    lv_label_set_text(lbl_wifi_status, "Offline Mode (Ch 1)");
    lv_obj_set_style_text_color(lbl_wifi_status, COLOR_CYAN, 0);
    lv_obj_set_style_text_font(lbl_wifi_status, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_wifi_status, LV_ALIGN_TOP_LEFT, 20, 35);

    // Vibration 
    lv_obj_t * lbl_vib = lv_label_create(card);
    lv_label_set_text(lbl_vib, "Vibration");
    lv_obj_set_style_text_color(lbl_vib, lv_color_white(), 0);
    lv_obj_align(lbl_vib, LV_ALIGN_TOP_LEFT, 20, 60);
    sw_vibration = lv_switch_create(card);
    lv_obj_align(sw_vibration, LV_ALIGN_TOP_RIGHT, -20, 55);
    lv_obj_add_state(sw_vibration, LV_STATE_CHECKED); // Default ON
    lv_obj_set_style_bg_color(sw_vibration, COLOR_CYAN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_vibration, toggle_vibration_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void build_nav_bar(void) {
    lv_obj_t * bot_bar = lv_obj_create(scr);
    lv_obj_set_size(bot_bar, 320, 50);
    lv_obj_align(bot_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bot_bar, lv_color_hex(0x050A0F), 0);
    lv_obj_set_style_border_side(bot_bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(bot_bar, lv_color_hex(0x1A2633), 0);
    lv_obj_clear_flag(bot_bar, LV_OBJ_FLAG_SCROLLABLE);

    const char * icons[] = {LV_SYMBOL_HOME, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS};
    for(int i=0; i<3; i++) {
        nav_labels[i] = lv_label_create(bot_bar);
        lv_label_set_text(nav_labels[i], icons[i]);
        lv_obj_set_style_text_font(nav_labels[i], &lv_font_montserrat_20, 0);
        lv_obj_align(nav_labels[i], LV_ALIGN_CENTER, (i-1)*100, 0);
        lv_obj_add_flag(nav_labels[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(nav_labels[i], nav_click_cb, LV_EVENT_CLICKED, (void*)(size_t)i);
    }
}

static void update_loop(lv_timer_t * timer) {
    static int minute_counter = 0;
    minute_counter++;
    if (minute_counter >= 2000) { 
        minute_counter = 0;
        lv_chart_set_next_value(chart_posture, ser_posture, (int)fabs(current_pitch));
    }

    water_timer_ticks++;
    if (water_timer_ticks > WATER_REMINDER_THRESHOLD) {
        water_alert_active = true;
    }

    int remaining = WATER_REMINDER_THRESHOLD - water_timer_ticks;
    if (remaining < 0) remaining = 0;
    
    int total_seconds = remaining * 0.03; 
    int mins = total_seconds / 60;
    int secs = total_seconds % 60;
    lv_label_set_text_fmt(label_water_timer, "%02d:%02d", mins, secs);

    posture_packet_t packet;
    if (xQueueReceive(posture_queue, &packet, 0) == pdTRUE) {
        last_packet_tick = xTaskGetTickCount();
        lv_obj_set_style_text_color(label_wifi_icon, COLOR_GREEN, 0);

        float p = packet.pitch;
        current_pitch = p;

        float raw_y = p * 1.5f; 
        if (raw_y > 45.0f) raw_y = 45.0f;
        if (raw_y < -45.0f) raw_y = -45.0f;

        lv_obj_align_to(posture_dot, spine_track, LV_ALIGN_CENTER, 0, (int)raw_y);
        lv_label_set_text_fmt(label_pitch_val, "P: %.0f", p);

        // --- PRIORITY HEADER: Water Alert > Slouch Alert > Good ---
        if (water_alert_active) {
            lv_label_set_text(label_posture_status, "DRINK WATER!");
            // Use Orange for high visibility alert
            lv_obj_set_style_text_color(label_posture_status, COLOR_ORANGE, 0);
        }
        else if (fabs(p) > 15.0f) {
            lv_obj_set_style_bg_color(posture_dot, COLOR_RED, 0);
            lv_obj_set_style_shadow_color(posture_dot, COLOR_RED, 0);
            
            lv_label_set_text(label_posture_status, "SLOUCH DETECTED");
            lv_obj_set_style_text_color(label_posture_status, COLOR_RED, 0);
        } else {
            lv_obj_set_style_bg_color(posture_dot, COLOR_CYAN, 0);
            lv_obj_set_style_shadow_color(posture_dot, COLOR_CYAN, 0);
            
            lv_label_set_text(label_posture_status, "POSTURE GOOD");
            lv_obj_set_style_text_color(label_posture_status, COLOR_GREEN, 0);
        }
    } 
    else {
        // Disconnected State
        if ((xTaskGetTickCount() - last_packet_tick) > pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS)) {
            lv_obj_set_style_text_color(label_wifi_icon, COLOR_TEXT_GRAY, 0); 
            lv_label_set_text(label_posture_status, "SEARCHING...");
            lv_obj_set_style_text_color(label_posture_status, COLOR_TEXT_GRAY, 0);
            
            lv_obj_align_to(posture_dot, spine_track, LV_ALIGN_CENTER, 0, 0);
        }
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_offline(); 
    init_esp_now();      

    bsp_display_start();
    bsp_display_backlight_on();
    bsp_display_lock(0);
    
    scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);

    label_posture_status = lv_label_create(scr);
    lv_label_set_text(label_posture_status, "WAITING...");
    lv_obj_set_style_text_font(label_posture_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_posture_status, lv_color_white(), 0);
    lv_obj_align(label_posture_status, LV_ALIGN_TOP_LEFT, 15, 10);

    label_wifi_icon = lv_label_create(scr);
    lv_label_set_text(label_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(label_wifi_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_wifi_icon, COLOR_TEXT_GRAY, 0);
    lv_obj_align(label_wifi_icon, LV_ALIGN_TOP_RIGHT, -15, 10);

    build_home_tab();
    build_stats_tab();
    build_settings_tab();
    
    build_nav_bar();
    switch_tab(0);

    lv_timer_create(update_loop, 30, NULL);
    
    bsp_display_unlock();
}