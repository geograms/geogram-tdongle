#define FASTLED_ALLOW_INTERRUPTS 0
#define FASTLED_ESP32_SPI
#define FASTLED_ESP32_SPI_PINS 1

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

void nextPosition() {}

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

}

void loop()
{
    button.tick();
    ble_tick(); 
    updateDisplay();
    updateTime();
    delay(5);

    /*
    if (millis() - lastRestart > restartInterval)
    {
        restartBeacon();
        lastRestart = millis();
    }
    */

}
