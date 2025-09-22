/**
 * @file webportal.cpp
 * @brief Async web portal implementation using ESPAsyncWebServer and StorageManager
 */

 #include <WiFi.h>
 #include <Preferences.h>
 #include <AsyncTCP.h>
 #include <ESPAsyncWebServer.h>
 #include "time_get.h"
 #include <esp_bt_device.h>
 #include "../API/API.h"
 #include "drive/storage.h"
 
 extern StorageManager storage;
 static AsyncWebServer server(80);
 
 /**
  * @brief Determines proper Content-Type based on file extension
  */
 String getContentType(String filename) {
     if(filename.endsWith(".html")) return "text/html";
     else if(filename.endsWith(".css")) return "text/css";
     else if(filename.endsWith(".js")) return "application/javascript";
     else if(filename.endsWith(".png")) return "image/png";
     else if(filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
     else if(filename.endsWith(".gif")) return "image/gif";
     else if(filename.endsWith(".ico")) return "image/x-icon";
     else if(filename.endsWith(".svg")) return "image/svg+xml";
     else if(filename.endsWith(".json")) return "application/json";
     return "text/plain";
 }
 
 /**
  * @brief Sets up captive portal-style routes to redirect clients to index.html.
  */
 void setupCaptivePortalRoutes() {
     server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
         request->redirect("/index.html");
     });
     server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
         request->redirect("/index.html");
     });
     server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
         request->send(200, "text/plain", "Microsoft NCSI");
     });
     server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
         request->redirect("/index.html");
     });
 }
 
 /**
  * @brief Serves static files dynamically from the currently active filesystem
  */
 void setupStaticFileHandler() {
     server.onNotFound([](AsyncWebServerRequest* request) {
         fs::FS& fs = storage.getActiveFS();
 
         if (request->host() == "captive.apple.com") {
             request->redirect("http://192.168.4.1");
             return;
         }
 
         String path = request->url();
         if (path.endsWith("/")) path += "index.html";
 
         Serial.printf("[Web] Looking for: %s in %s\n", path.c_str(), storage.isUsingSD() ? "SD" : "LittleFS");
 
         if (fs.exists(path)) {
             String contentType = getContentType(path);
             request->send(fs, path, contentType);
         } else {
             request->send(404, "text/plain", "File Not Found");
         }
     });
 }
 
 /**
  * @brief Handles uptime status response as JSON at /api/status
  */
 void setupStatusHandler() {
     server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
         auto pairs = handleRequest("/status/get", {});
 
         String jsonResponse = "{";
         bool first = true;
 
         for (const auto& pair : pairs) {
             if (!first) jsonResponse += ",";
             first = false;
 
             if (pair.first == "error" || pair.first == "status") {
                 jsonResponse += "\"" + pair.first + "\":\"" + pair.second + "\"";
             } else {
                 if (pair.second.length() == 0) {
                     jsonResponse += "\"" + pair.first + "\":null";
                 } else if (pair.first == "uptime" || pair.first == "storage") {
                     jsonResponse += "\"" + pair.first + "\":\"" + pair.second + "\"";
                 } else {
                     bool isNumeric = true;
                     for (unsigned int i = 0; i < pair.second.length(); i++) {
                         if (!isdigit(pair.second[i])) {
                             isNumeric = false;
                             break;
                         }
                     }
                     jsonResponse += isNumeric ?
                         "\"" + pair.first + "\":" + pair.second :
                         "\"" + pair.first + "\":\"" + pair.second + "\"";
                 }
             }
         }
 
         jsonResponse += "}";
         request->send(200, "application/json", jsonResponse);
     });
 }
 
 /**
  * @brief Handles homepage API response at /api/homepage
  */
 void setupHomepageHandler() {
     server.on("/api/homepage", HTTP_GET, [](AsyncWebServerRequest* request) {
         auto pairs = handleRequest("/homepage", {});
         for (const auto& pair : pairs) {
             if (pair.first == "json") {
                 request->send(200, "application/json", pair.second);
                 return;
             }
         }
         request->send(500, "application/json", "{\"error\":\"failed to build homepage\"}");
     });
 }
 
 /**
  * @brief Starts the web portal, initializes FS, Wi-Fi, and Async server.
  */
 void startWebPortal() {
     if (!storage.begin()) {
         Serial.println("Storage init failed.");
         return;
     }
 
     Preferences prefs;
     prefs.begin("config", true);
     String suffix = prefs.getString("wifi_hotspot_name", "");
     String wifi_ssid = prefs.getString("wifi_ssid", "");
     String wifi_password = prefs.getString("wifi_password", "");
     prefs.end();
 
     String hotspotSSID = (suffix.length() > 0) ? ("geogram-" + suffix) : "geogram";
 
     WiFi.mode(WIFI_AP_STA);
     WiFi.softAP(hotspotSSID.c_str(), "");
 
     if (wifi_ssid.length() > 0) {
         Serial.print("Connecting to Wi-Fi: ");
         Serial.println(wifi_ssid);
         WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
 
         unsigned long start = millis();
         while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
             delay(100);
         }
 
         if (WiFi.status() == WL_CONNECTED) {
             Serial.print("Connected, IP: ");
             Serial.println(WiFi.localIP());
             updateTime();
         } else {
             Serial.println("Wi-Fi connection failed.");
         }
     }
 
     setupCaptivePortalRoutes();
     setupStaticFileHandler();
     setupStatusHandler();
     setupHomepageHandler();
 
     server.begin();
     Serial.println("Async Web portal active at: http://192.168.4.1");
 }
 