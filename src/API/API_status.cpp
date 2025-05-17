#include <Preferences.h>
#include <WiFi.h>
#include <esp_bt_device.h>
#include "API.h"

static String getUptime() {
    const unsigned long totalSeconds = millis() / 1000;
    const unsigned long seconds = totalSeconds % 60;
    const unsigned long minutes = (totalSeconds / 60) % 60;
    const unsigned long hours = (totalSeconds / 3600) % 24;
    const unsigned long days = (totalSeconds / 86400) % 30;
    const unsigned long months = (totalSeconds / 2592000) % 12;
    const unsigned long years = totalSeconds / 31536000;

    struct TimeUnit {
        unsigned long value;
        const char* singular;
        const char* plural;
    };

    TimeUnit units[] = {
        {years, "year", "years"},
        {months, "month", "months"},
        {days, "day", "days"},
        {hours, "hour", "hours"},
        {minutes, "minute", "minutes"}
    };

    String result;
    int count = 0;
    
    // First count how many units we'll display
    for (const auto& unit : units) {
        if (unit.value > 0) count++;
    }

    // Special case for just booted
    if (count == 0) {
        return "0 minutes";
    }

    int displayed = 0;
    for (const auto& unit : units) {
        if (unit.value > 0) {
            displayed++;
            if (!result.isEmpty()) {
                if (displayed == count) {
                    result += " and ";
                } else {
                    result += ", ";
                }
            }
            result += String(unit.value) + " " + (unit.value == 1 ? unit.singular : unit.plural);
        }
    }

    return result;
}

static String getWiFiMac() {
    return WiFi.softAPmacAddress();
}

static String getBtMac() {
    const uint8_t* btMacRaw = esp_bt_dev_get_address();
    char btMac[18];
    snprintf(btMac, sizeof(btMac), "%02X:%02X:%02X:%02X:%02X:%02X",
             btMacRaw[0], btMacRaw[1], btMacRaw[2],
             btMacRaw[3], btMacRaw[4], btMacRaw[5]);
    return String(btMac);
}

static String getStorage() {
    return "";
}

static String getMediaCount() {
    return "";
}

static String getMessageCount() {
    return "";
}

std::vector<std::pair<String, String>> handleRequestStatus(const String& path, const std::vector<std::pair<String, String>>& params) {
    std::vector<std::pair<String, String>> response;

    if (path == "/status/get") {
        response.emplace_back("uptime", getUptime());
        response.emplace_back("wifi_mac", getWiFiMac());
        response.emplace_back("bt_mac", getBtMac());
        response.emplace_back("storage", getStorage());
        response.emplace_back("media_count", getMediaCount());
        response.emplace_back("message_count", getMessageCount());
        response.emplace_back("status", "ok");
    } else {
        response.emplace_back("error", "invalid path");
    }

    return response;
}
