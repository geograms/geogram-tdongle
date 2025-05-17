#include <WebServer.h>
#include <ArduinoJson.h>
#include "apps/presence.h"
#include "drive/storage.h"

extern WebServer server;
extern StorageManager storage;

int calculateMinutes(const String& deviceId, int daysAgoStart, int daysAgoEnd) {
    time_t now = time(nullptr);
    time_t end = now - daysAgoStart * 86400;
    time_t start = now - daysAgoEnd * 86400;
    return countPresenceMinutes(deviceId, start, end);
}

void handleStatsApi() {
    fs::FS& fs = storage.getActiveFS();

    File root = fs.open("/");
    if (!root || !root.isDirectory()) {
        server.send(500, "application/json", "{\"error\":\"Failed to open root\"}");
        return;
    }

    StaticJsonDocument<8192> doc;

    File entry = root.openNextFile();
    while (entry) {
        String deviceId = entry.name();
        if (deviceId.startsWith("/") && entry.isDirectory()) {
            deviceId.remove(0, 1); // remove leading "/"
            if (fs.exists("/" + deviceId + "/presence")) {
                JsonObject dev = doc.createNestedObject(deviceId);
                dev["today"] = calculateMinutes(deviceId, 0, 0);
                dev["last5"] = calculateMinutes(deviceId, 0, 4);
                dev["last30"] = calculateMinutes(deviceId, 0, 29);
            }
        }
        entry = root.openNextFile();
    }

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void setupStatsRoutes() {
    server.on("/api/stats", HTTP_GET, handleStatsApi);
}
