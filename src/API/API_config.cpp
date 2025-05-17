#include <Preferences.h>
#include "API.h"

static const int maxLength = 50;

static const std::vector<std::pair<String, String>> configKeys = {
    {"wifi_ssid", "Network to which you want to connect this device"},
    {"wifi_password", "Password for the target Wi-Fi network"},
    {"wifi_hotspot_name", "Name for this device"},
    {"config_password", "Password required to access configuration (optional)"}
};

std::vector<std::pair<String, String>> handleRequestConfig(const String& path, const std::vector<std::pair<String, String>>& params) {
    std::vector<std::pair<String, String>> response;
    Preferences prefs;

    if (path == "/config/get") {
        prefs.begin("config", true);
        for (const auto& entry : configKeys) {
            String key = entry.first;
            String desc = entry.second;
            String value = prefs.getString(key.c_str(), "");
            if (value.length() > maxLength) value = value.substring(0, maxLength);
            response.emplace_back(key, value);
            response.emplace_back(key + "_description", desc);
        }
        prefs.end();
        response.emplace_back("status", "ok");
    }

    else if (path == "/config/set") {
        prefs.begin("config", false);
        for (const auto& kv : params) {
            for (const auto& entry : configKeys) {
                if (kv.first == entry.first) {
                    String val = kv.second;
                    if (val.length() > maxLength) val = val.substring(0, maxLength);
                    prefs.putString(kv.first.c_str(), val);
                    break;
                }
            }
        }
        prefs.end();
        response.emplace_back("status", "ok");
    }

    else {
        response.emplace_back("error", "invalid path");
    }

    return response;
}
