#include <TFT_eSPI.h>
#include <lvgl.h>
#include <WiFi.h>
#include <Preferences.h>
#include "lv_driver.h"
#include "misc/pinconfig.h"
#include "inspiration.h"

TFT_eSPI screen = TFT_eSPI();
static lv_obj_t* status_label = nullptr;
static lv_obj_t* log_box = nullptr;
static lv_obj_t* ip_label = nullptr;
static lv_obj_t* device_count_label = nullptr;
static String log_lines[1000];
static uint16_t log_index = 0;

void writeLog(const char* line) {
    if (strncmp(line, "Remember:", 9) != 0 &&
        strncmp(line, "Stay alert:", 11) != 0 &&
        strncmp(line, "Off the grid:", 13) != 0 &&
        strncmp(line, "Pro tip:", 8) != 0 &&
        strncmp(line, "Don't forget:", 13) != 0 &&
        strncmp(line, "In silence:", 11) != 0 &&
        strncmp(line, "Your system:", 13) != 0 &&
        strncmp(line, "Legacy fades:", 14) != 0 &&
        strncmp(line, "Think different:", 16) != 0 &&
        strncmp(line, "Trust yourself:", 16) != 0) {
        Serial.println(line);
    }

    if (log_index >= 1000) {
        for (int i = 1; i < 1000; ++i) {
            log_lines[i - 1] = log_lines[i];
        }
        log_index = 999;
    }

    log_lines[log_index++] = line;

    String content;
    for (int i = 0; i < log_index; ++i) {
        content += log_lines[i];
        if (i != log_index - 1) content += "\n";
    }

    lv_textarea_set_text(log_box, content.c_str());
    lv_textarea_set_cursor_pos(log_box, LV_TEXTAREA_CURSOR_LAST);
}

void clearLog() {
    log_index = 0;
    lv_textarea_set_text(log_box, "");
}

void initDisplay() {
    screen.init();
    screen.setRotation(1);
    screen.fillScreen(TFT_BLACK);
    pinMode(TFT_LEDA_PIN, OUTPUT);
    digitalWrite(TFT_LEDA_PIN, 0);

    screen.setTextFont(1);
    screen.setTextColor(TFT_GREEN, TFT_BLACK);
    delay(1000);

    Serial.begin(115200);

    lvgl_init();

    lv_theme_t* dark = lv_theme_default_init(nullptr,
                                             lv_palette_main(LV_PALETTE_BLUE),
                                             lv_palette_main(LV_PALETTE_GREY),
                                             true,
                                             &lv_font_montserrat_10);
    lv_disp_set_theme(nullptr, dark);

    lv_obj_t* status_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_bar, LV_HOR_RES, 20);
    lv_obj_set_style_bg_color(status_bar, lv_color_make(255, 140, 0), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);

    status_label = lv_label_create(status_bar);
    lv_label_set_text(status_label, "ecogram uptime: 00:00:00");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 1, 0);

    const int bottom_bar_height = 14;
    lv_obj_t* bottom_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bottom_bar, LV_HOR_RES, bottom_bar_height);
    lv_obj_set_style_bg_color(bottom_bar, lv_color_make(128, 128, 128), 0);
    lv_obj_set_style_border_width(bottom_bar, 0, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);

    device_count_label = lv_label_create(bottom_bar);
    lv_label_set_text(device_count_label, "");
    lv_obj_set_style_text_font(device_count_label, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(device_count_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(device_count_label, LV_ALIGN_LEFT_MID, 4, 0);

    ip_label = lv_label_create(bottom_bar);
    lv_label_set_text(ip_label, "IP: unknown");
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(ip_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(ip_label, LV_ALIGN_RIGHT_MID, -4, 0);

    int log_box_height = LV_VER_RES - 20 - bottom_bar_height;
    log_box = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(log_box, LV_HOR_RES, log_box_height);
    lv_obj_align(log_box, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_textarea_set_max_length(log_box, 32767);
    lv_textarea_set_text(log_box, "--");
    lv_obj_set_style_text_font(log_box, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(log_box, lv_color_black(), 0);
    lv_obj_set_style_text_color(log_box, lv_color_white(), 0);
    lv_obj_set_style_border_width(log_box, 1, 0);
    lv_obj_set_style_border_color(log_box, lv_color_black(), 0);
    lv_obj_set_scrollbar_mode(log_box, LV_SCROLLBAR_MODE_OFF);

    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_style_all(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);
}

void updateDisplay() {
    lv_timer_handler();

    static uint32_t last_sec = 0;
    uint32_t total_sec = millis() / 1000;
    if (total_sec != last_sec && status_label) {
        last_sec = total_sec;

        uint32_t days = total_sec / 86400;
        uint32_t hours = (total_sec / 3600) % 24;
        uint32_t minutes = (total_sec / 60) % 60;
        uint32_t seconds = total_sec % 60;

        static char buf[64];
        if (days == 0) {
            snprintf(buf, sizeof(buf), "ecogram uptime: %02u:%02u:%02u", hours, minutes, seconds);
        } else {
            snprintf(buf, sizeof(buf), "ecogram uptime: %lu day%s %02u h",
                     days, (days == 1 ? "" : "s"), hours);
        }
        lv_label_set_text(status_label, buf);
    }

    Preferences prefs;
    prefs.begin("stats", true);
    int count = prefs.getInt("users_detected", 0);
    prefs.end();

    static int last_count = 0;
    if (count > 0 && device_count_label) {
        last_count = count;
        char buf[8];
        snprintf(buf, sizeof(buf), "x%d", count);
        lv_label_set_text(device_count_label, buf);
    }else{
        char buf[8];
        snprintf(buf, sizeof(buf), "");
        lv_label_set_text(device_count_label, buf);
    }

    static uint32_t last_inspiration = 0;
    if (millis() - last_inspiration > 300000) {
        last_inspiration = millis();
        clearLog();
        generateInspiration();
    }

    IPAddress ip = WiFi.isConnected() ? WiFi.localIP() : WiFi.softAPIP();
    static String last_ip = "";
    String current_ip = "IP: " + ip.toString();
    if (current_ip != last_ip && ip_label) {
        last_ip = current_ip;
        lv_label_set_text(ip_label, current_ip.c_str());
    }
}