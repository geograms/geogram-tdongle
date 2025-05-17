#include "time_get.h"
#include <WiFi.h>
#include <time.h>

static bool timeInitialized = false;
static unsigned long lastAttempt = 0;
static const unsigned long retryIntervalMs = 5 * 60 * 1000; // 5 minutes

void initTime() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    Serial.println("[time] Waiting for time sync...");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        Serial.println("[time] Time synchronized.");
        timeInitialized = true;
    } else {
        Serial.println("[time] Failed to get time.");
        lastAttempt = millis();
    }
}

void updateTime() {
    if (timeInitialized) return;

    if (WiFi.status() != WL_CONNECTED) return;

    unsigned long now = millis();
    if (now - lastAttempt >= retryIntervalMs || lastAttempt == 0) {
        lastAttempt = now;

        Serial.println("[time] Retrying time sync...");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");

        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            Serial.println("[time] Time synchronized.");
            timeInitialized = true;
        } else {
            Serial.println("[time] Time sync failed.");
        }
    }
}

time_t getCurrentTimestamp() {
    return time(nullptr);
}
