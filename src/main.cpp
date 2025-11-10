#define FASTLED_ALLOW_INTERRUPTS 0
#define FASTLED_ESP32_SPI
#define FASTLED_ESP32_SPI_PINS 1

// Device model and version for identification
#define DEVICE_MODEL "LT1"  // LilyGo T-Dongle
#define DEVICE_VERSION "0.0.1"

#include "misc/pin_config.h"
#include "misc/pinconfig.h"
#include <TFT_eSPI.h>
#include <OneButton.h>
#include <FastLED.h>
#include <Preferences.h>
#include <EEPROM.h>
#include "ble/ble.h"
#include "display/display.h"
#include "display/inspiration.h"
#include "wifi/time_get.h"
#include "drive/storage.h"

extern void startWebPortal();
StorageManager storage;

OneButton button(BTN_PIN, true);
CRGB leds;
#define TOUCH_CS -1

unsigned long lastRestart = 0;
const unsigned long restartInterval = 60000;

unsigned long lastPingTime = 0;
const unsigned long pingInterval = 10000; // 10 seconds

void nextPosition() {}
void blinkLED(); // Forward declaration

// Generate a random callsign starting with X1 followed by 4 alphanumeric characters
String generateRandomCallsign() {
    const char alphanumeric[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    String callsign = "X1";
    for (int i = 0; i < 4; i++) {
        callsign += alphanumeric[random(0, 36)];
    }
    return callsign;
}

// Get or generate callsign from preferences
String getOrCreateCallsign() {
    Preferences prefs;
    prefs.begin("config", false); // Read-write mode
    String callsign = prefs.getString("callsign", ""); // Shorter key name (max 15 chars)

    // If no callsign exists or it's still the default, generate a new one
    if (callsign.length() == 0 || callsign == "geogram") {
        callsign = generateRandomCallsign();
        prefs.putString("callsign", callsign);
        Serial.print("Generated new callsign: ");
        Serial.println(callsign);
    } else {
        Serial.print("Using existing callsign: ");
        Serial.println(callsign);
    }

    prefs.end();
    return callsign;
}

// Send Bluetooth ping with callsign
void sendBluetoothPing() {
    static String callsign = ""; // Cache the callsign

    if (callsign.length() == 0) {
        callsign = getOrCreateCallsign();
    }

    // Don't include '>' prefix - ble_send_text adds it automatically
    // Include device model code and version: +CALLSIGN#LT1-0.0.1
    String pingMsg = "+" + callsign + "#" + DEVICE_MODEL + "-" + DEVICE_VERSION;

    if (pingMsg.length() <= 30) { // BLE payload limit for compact device codes
        int result = ble_send_text((const uint8_t*)pingMsg.c_str(), pingMsg.length(), true);
        if (result > 0) {
            Serial.print("Ping sent: >");
            Serial.println(pingMsg);
            blinkLED(); // Visual feedback
        } else {
            Serial.println("Failed to send ping");
        }
    } else {
        Serial.println("Ping message too long");
    }
}

void blinkLED()
{
    leds = CRGB::White;
    FastLED.setBrightness(64);
    FastLED.show();
    delay(100);
    leds = CRGB::Black;
    FastLED.show();
}

void setup()
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(0);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    pinMode(TFT_LEDA_PIN, OUTPUT);
    digitalWrite(TFT_LEDA_PIN, 1);

    Serial.begin(115200);
    EEPROM.begin(1);

    initDisplay();

    leds = CRGB(0, 0, 0);
    FastLED.addLeds<APA102, LED_DI_PIN, LED_CI_PIN, BGR>(&leds, 1);
    FastLED.show();

    button.attachClick(nextPosition);
    digitalWrite(TFT_LEDA_PIN, 0);

    // Mount the SD card
    if (storage.begin())
    {
        Serial.println("Storage ready.");
        storage.listDir("/", 2); // Optional: show root directory
    }
    else
    {
        Serial.println("Storage initialization failed.");
    }

    startWebPortal();

    Preferences prefs;
    prefs.begin("config", true);
    String suffix = prefs.getString("wifi_hotspot_name", "geogram");
    prefs.end();

    String id = suffix;
    String beaconName = //"geogram-" + 
        suffix;
   
    String msg = "";

    // Generate proper namespace and instance IDs from your suffix
    String namespaceId = "0000000000"; // 10-byte namespace (adjust as needed)
    String instanceId = "000001";      // 6-byte instance (adjust as needed)

    //startBeacon(beaconName.c_str(), namespaceId.c_str(), instanceId.c_str());

    initTime();

    blinkLED();
    lastRestart = millis();

    generateInspiration();

    ble_init("ESP32-TDongle");
    ble_start_listening(true);

    // Initialize callsign and set first ping to happen 10 seconds after boot
    getOrCreateCallsign();
    lastPingTime = millis() - pingInterval + 10000; // First ping in 10 seconds

}

void loop()
{
    button.tick();
    ble_tick();
    updateDisplay();
    updateTime();

    // Send Bluetooth ping every 10 seconds with random delay to avoid collisions
    unsigned long now = millis();
    if (now - lastPingTime >= pingInterval) {
        lastPingTime = now;
        // Add random delay 0-500ms to avoid all devices transmitting simultaneously
        delay(random(0, 500));
        sendBluetoothPing();
    }

    delay(5);

    /*
    if (millis() - lastRestart > restartInterval)
    {
        restartBeacon();
        lastRestart = millis();
    }
    */

}
