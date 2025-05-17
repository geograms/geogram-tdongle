/**
 * @file presence.cpp
 * @brief Presence detection and management
 */

 #include <Arduino.h>
 #include <ArduinoJson.h>
 #include <map>
 #include <memory>
 #include "drive/storage.h"
 
 extern StorageManager storage;
 
 static const int bitmapSize = 1440; // 24 hours * 60 minutes
 static const int maxInactivityMin = 2;
 
 struct PresenceRange {
     time_t start;
     time_t lastSeen;
 };
 
 static std::map<String, PresenceRange> activeDevices;
 
 static String zeroPad(int v) {
     return (v < 10 ? "0" : "") + String(v);
 }
 
 static String getMonthFilePath(const String& deviceId, int year, int month) {
     return "/" + deviceId + "/presence/" + String(year) + "/" + zeroPad(month) + ".json";
 }
 
 void updatePresence(const String& deviceId, time_t timestamp) {
     struct tm* tmInfo = localtime(&timestamp);
     if (!tmInfo) return;
 
     int day = tmInfo->tm_mday;
     int month = tmInfo->tm_mon + 1;
     int year = tmInfo->tm_year + 1900;
     int minuteOfDay = tmInfo->tm_hour * 60 + tmInfo->tm_min;
 
     PresenceRange& state = activeDevices[deviceId];
 
     if (state.start == 0 || timestamp - state.lastSeen > maxInactivityMin * 60) {
         state.start = timestamp;
     }
 
     state.lastSeen = timestamp;
 
     String path = getMonthFilePath(deviceId, year, month);
     fs::FS& fs = storage.getActiveFS();
 
     std::unique_ptr<StaticJsonDocument<32 * 1024>> doc(new StaticJsonDocument<32 * 1024>);
 
     if (fs.exists(path)) {
         Serial.printf("[presence] Reading file %s\n", path.c_str());
         File f = fs.open(path, "r");
         DeserializationError err = deserializeJson(*doc, f);
         f.close();
         if (err) {
             Serial.printf("[presence] Failed to parse JSON: %s\n", err.c_str());
             return;
         }
     }
 
     String dayStr = String(day);
     String bitmap;
 
     if ((*doc).containsKey(dayStr)) {
         bitmap = (*doc)[dayStr].as<String>();
     } else {
         char buf[bitmapSize + 1];
         memset(buf, '0', bitmapSize);
         buf[bitmapSize] = '\0';
         bitmap = String(buf);
     }
 
     if (minuteOfDay >= 0 && minuteOfDay < bitmapSize) {
         bitmap.setCharAt(minuteOfDay, '1');
     }
 
     (*doc)[dayStr] = bitmap;
 
     String folder = "/" + deviceId + "/presence/" + String(year);
     fs.mkdir("/" + deviceId);
     fs.mkdir("/" + deviceId + "/presence");
     fs.mkdir(folder);
 
     File out = fs.open(path, "w");
     if (serializeJson(*doc, out)) {
         Serial.printf("[presence] Updated %s\n", path.c_str());
     } else {
         Serial.printf("[presence] Failed to write to %s\n", path.c_str());
     }
     out.close();
 }
 
 int countPresenceMinutes(const String& deviceId, time_t start, time_t end) {
     struct tm tmStart = *localtime(&start);
     struct tm tmEnd = *localtime(&end);
     int total = 0;
 
     fs::FS& fs = storage.getActiveFS();
 
     for (int y = tmStart.tm_year + 1900; y <= tmEnd.tm_year + 1900; ++y) {
         int mStart = (y == tmStart.tm_year + 1900) ? tmStart.tm_mon + 1 : 1;
         int mEnd = (y == tmEnd.tm_year + 1900) ? tmEnd.tm_mon + 1 : 12;
 
         for (int m = mStart; m <= mEnd; ++m) {
             String path = getMonthFilePath(deviceId, y, m);
             if (!fs.exists(path)) continue;
 
             File f = fs.open(path, "r");
             std::unique_ptr<StaticJsonDocument<32 * 1024>> doc(new StaticJsonDocument<32 * 1024>);
             DeserializationError err = deserializeJson(*doc, f);
             f.close();
             if (err) continue;
 
             for (JsonPair kv : doc->as<JsonObject>()) {
                 int day = String(kv.key().c_str()).toInt();
                 struct tm dayTm = {};
                 dayTm.tm_year = y - 1900;
                 dayTm.tm_mon = m - 1;
                 dayTm.tm_mday = day;
                 dayTm.tm_hour = 0;
                 dayTm.tm_min = 0;
                 dayTm.tm_sec = 0;
                 time_t dayStart = mktime(&dayTm);
                 time_t dayEnd = dayStart + 86399;
 
                 if (dayEnd < start || dayStart > end) continue;
 
                 const char* bitmap = kv.value().as<const char*>();
                 if (!bitmap) continue;
 
                 for (int i = 0; i < bitmapSize; ++i) {
                     time_t t = dayStart + i * 60;
                     if (t >= start && t <= end && bitmap[i] == '1') {
                         total++;
                     }
                 }
             }
         }
     }
 
     return total;
 }
 