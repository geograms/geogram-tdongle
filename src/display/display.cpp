#include <TFT_eSPI.h>
#include <lvgl.h>
#include <WiFi.h>
#include <Preferences.h>
#include "lv_driver.h"
#include "misc/pinconfig.h"

// BLE event interface (loose coupling)
#include "ble/ble.h"

TFT_eSPI screen = TFT_eSPI();

static lv_obj_t* status_label = nullptr;
static lv_obj_t* ip_label = nullptr;
static lv_obj_t* device_count_label = nullptr;
static lv_obj_t* msg_container = nullptr;
static lv_obj_t* msg_label = nullptr;

// -------- last N messages (updated by BLE events, applied in updateDisplay) --------
static constexpr uint8_t MSG_SHOW_MAX = 3;
static constexpr size_t  MSG_LINE_CAP = 256;          // per-line cap to avoid heap churn
static char  s_msgs[MSG_SHOW_MAX][MSG_LINE_CAP];      // newest at index 0
static uint8_t s_msgs_cnt = 0;
static volatile bool s_msgs_dirty = false;

// ---------------- BLE event → store last 3 messages (no LVGL calls here) ----------------
static void on_ble_event(const BleEvent* e, void* /*ctx*/) {
    if (!e) return;

    switch (e->type) {
        case BLE_EVT_MESSAGE_DONE: {
            const char* from     = e->data.done.from;
            const char* snippet  = e->data.done.snippet;
            const uint32_t mlen  = e->data.done.msg_len;

            const char* from_s = (from && from[0]) ? from : "---";
            const char* snip_s = snippet ? snippet : "";

            char line[MSG_LINE_CAP];
            if (mlen > strlen(snip_s)) {
                snprintf(line, sizeof(line), "%s: %s…", from_s, snip_s);
            } else {
                snprintf(line, sizeof(line), "%s: %s", from_s, snip_s);
            }

            // Append at the end; keep only the last MSG_SHOW_MAX messages
            if (s_msgs_cnt < MSG_SHOW_MAX) {
                strncpy(s_msgs[s_msgs_cnt], line, MSG_LINE_CAP - 1);
                s_msgs[s_msgs_cnt][MSG_LINE_CAP - 1] = '\0';
                s_msgs_cnt++;
            } else {
                // drop oldest by shifting up, put new at the last slot
                for (uint8_t i = 1; i < MSG_SHOW_MAX; ++i) {
                    strncpy(s_msgs[i - 1], s_msgs[i], MSG_LINE_CAP - 1);
                    s_msgs[i - 1][MSG_LINE_CAP - 1] = '\0';
                }
                strncpy(s_msgs[MSG_SHOW_MAX - 1], line, MSG_LINE_CAP - 1);
                s_msgs[MSG_SHOW_MAX - 1][MSG_LINE_CAP - 1] = '\0';
            }

            s_msgs_dirty = true;
            break;
        }
        default:
            break;
    }
}


// ---------------- UI init/update ----------------
void initDisplay() {
    screen.init();
    screen.setRotation(1);
    screen.fillScreen(TFT_BLACK);
    pinMode(TFT_LEDA_PIN, OUTPUT);
    digitalWrite(TFT_LEDA_PIN, 0);

    screen.setTextFont(1);
    screen.setTextColor(TFT_GREEN, TFT_BLACK);
    delay(1000);

    // Serial may already be started in main; keeping this is harmless
    Serial.begin(115200);

    lvgl_init();

    lv_theme_t* dark = lv_theme_default_init(nullptr,
                                             lv_palette_main(LV_PALETTE_BLUE),
                                             lv_palette_main(LV_PALETTE_GREY),
                                             true,
                                             &lv_font_montserrat_10);
    lv_disp_set_theme(nullptr, dark);

    // Status bar (top)
    lv_obj_t* status_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_bar, LV_HOR_RES, 20);
    lv_obj_set_style_bg_color(status_bar, lv_color_make(255, 140, 0), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);

    status_label = lv_label_create(status_bar);
    lv_label_set_text(status_label, "geogram uptime: 00:00:00");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_LEFT_MID, 1, 0);

    // Bottom bar
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

    // Center message area (fully black, no borders)
    int center_h = LV_VER_RES - 20 - bottom_bar_height;
    msg_container = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(msg_container);
    lv_obj_set_size(msg_container, LV_HOR_RES, center_h);
    lv_obj_align(msg_container, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_obj_set_style_bg_color(msg_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(msg_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(msg_container, 0, 0);
    lv_obj_set_style_pad_all(msg_container, 6, 0);

    // Make the container scrollable so we can scroll to bottom for long messages
    lv_obj_set_scrollbar_mode(msg_container, LV_SCROLLBAR_MODE_AUTO);

    msg_label = lv_label_create(msg_container);
    lv_label_set_text(msg_label, "--");
    lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(msg_label, lv_color_white(), 0);
    lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_label, LV_PCT(100));
    lv_obj_align(msg_label, LV_ALIGN_TOP_LEFT, 0, 0);

    // Screen style
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_style_all(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    // Subscribe to BLE events AFTER UI is ready
    ble_subscribe(on_ble_event, nullptr);

    // Clear buffers
    for (uint8_t i = 0; i < MSG_SHOW_MAX; ++i) s_msgs[i][0] = '\0';
    s_msgs_cnt = 0;
    s_msgs_dirty = false;
}

void updateDisplay() {
    // Pump LVGL
    lv_timer_handler();

    // Uptime label
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
            snprintf(buf, sizeof(buf), "geogram uptime: %02u:%02u:%02u", hours, minutes, seconds);
        } else {
            snprintf(buf, sizeof(buf), "geogram uptime: %lu day%s %02u h",
                     days, (days == 1 ? "" : "s"), hours);
        }
        lv_label_set_text(status_label, buf);
    }

    // Devices count (from Preferences)
    Preferences prefs;
    prefs.begin("stats", true);
    int count = prefs.getInt("users_detected", 0);
    prefs.end();

    if (device_count_label) {
        if (count > 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "x%d", count);
            lv_label_set_text(device_count_label, buf);
        } else {
            lv_label_set_text(device_count_label, "");
        }
    }

    // Apply messages to UI
    if (s_msgs_dirty) {
        s_msgs_dirty = false;

        if (msg_label) {
            // Combine up to last 3 messages into one wrapped label (newest first)
            char combined[MSG_SHOW_MAX * MSG_LINE_CAP + 8];
            combined[0] = '\0';

            for (uint8_t i = 0; i < s_msgs_cnt; ++i) {
                strncat(combined, s_msgs[i], sizeof(combined) - 1 - strlen(combined));
                if (i + 1 < s_msgs_cnt) {
                    strncat(combined, "\n", sizeof(combined) - 1 - strlen(combined));
                }
            }

            lv_label_set_text(msg_label, (s_msgs_cnt == 0) ? "--" : combined);

            // Ensure layout and scroll to bottom so the newest end is visible
            lv_obj_update_layout(msg_container);
            lv_obj_scroll_to_y(msg_container, LV_COORD_MAX, LV_ANIM_OFF);
        }
    }

    // IP label
    IPAddress ip = WiFi.isConnected() ? WiFi.localIP() : WiFi.softAPIP();
    static String last_ip = "";
    String current_ip = "IP: " + ip.toString();
    if (current_ip != last_ip && ip_label) {
        last_ip = current_ip;
        lv_label_set_text(ip_label, current_ip.c_str());
    }
}
