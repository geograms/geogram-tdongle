#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "apps/presence.h"
#include "wifi/time_get.h"

extern TFT_eSPI screen;

// Persistent advertising object
static BLEAdvertising* adv = nullptr;
static BLEAdvertisementData advData;
static BLEAdvertisementData scanResponseData;

void scanNearbyEddystoneUIDs() {
    BLEScan* scanner = BLEDevice::getScan();
    scanner->setActiveScan(true);
    scanner->setInterval(100);
    scanner->setWindow(99);

    BLEScanResults results = scanner->start(5, false); // 5 seconds scan
    int count = results.getCount();

    Serial.printf("Found %d BLE devices\n", count);
    int eddystoneCount = 0;

    for (int i = 0; i < count; ++i) {
        BLEAdvertisedDevice d = results.getDevice(i);
        if (d.haveServiceData()) {
            std::string data = d.getServiceData();
            if (d.haveServiceUUID() && d.getServiceUUID().toString() == "0000feaa-0000-1000-8000-00805f9b34fb") {
                uint8_t frameType = data[0];
                if (frameType == 0x00 && data.length() >= 18) { // UID frame
                    eddystoneCount++;
                    Serial.println("Eddystone UID found:");
                    Serial.print("  Namespace ID: ");
                    char deviceId[21] = {0}; // 10 bytes x 2 + null
                    for (int j = 2; j < 12; ++j) {
                        Serial.printf("%02X", (uint8_t)data[j]);
                        sprintf(&deviceId[(j - 2) * 2], "%02X", (uint8_t)data[j]);
                    }

                    Serial.print("\n  Instance ID: ");
                    for (int j = 12; j < 18; ++j) {
                        Serial.printf("%02X", (uint8_t)data[j]);
                    }
                    Serial.println();

                    updatePresence(String(deviceId), getCurrentTimestamp());
                }
            }
        }
    }

    Preferences prefs;
    prefs.begin("stats", false);
    prefs.putInt("users_detected", eddystoneCount);
    prefs.end();
    Serial.printf("[BLE] Eddystone devices detected: %d\n", eddystoneCount);

    scanner->clearResults();
}

void startBeacon(const char* beaconName, const char* namespaceId, const char* instanceId) {
    BLEDevice::init(beaconName);
    adv = BLEDevice::getAdvertising();
    
    // Create proper Eddystone UID frame
    std::string serviceData;
    serviceData += (char)0x00; // Eddystone UID frame type
    serviceData += (char)0xBA; // Calibrated TX power @ 0m (-70 dBm typical)

    // Convert hex namespace ID to bytes (10 bytes)
    for (int i = 0; i < 20; i += 2) {
        std::string byteStr = std::string(namespaceId + i, 2);
        char byte = (char)strtoul(byteStr.c_str(), NULL, 16);
        serviceData += byte;
    }
    
    // Convert hex instance ID to bytes (6 bytes)
    for (int i = 0; i < 12; i += 2) {
        std::string byteStr = std::string(instanceId + i, 2);
        char byte = (char)strtoul(byteStr.c_str(), NULL, 16);
        serviceData += byte;
    }
    
    // Add 2 bytes Reserved For Future Use (RFU) — must be zero
    serviceData += (char)0x00;
    serviceData += (char)0x00;

    // Configure advertisement data
    advData = BLEAdvertisementData();
    advData.setFlags(0x06); // General discoverable, no BR/EDR
    advData.setServiceData(BLEUUID((uint16_t)0xFEAA), serviceData);
    
    // Configure scan response data
    scanResponseData = BLEAdvertisementData();
    scanResponseData.setName(beaconName);
    
    // Set advertising data
    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanResponseData);
    
    // Add service UUID to advertising
    adv->addServiceUUID(BLEUUID((uint16_t)0xFEAA));
    
    // Start advertising
    adv->start();
    
    Serial.println("Eddystone Beacon started successfully");
}



void restartBeacon() {
    if (adv) {
        adv->stop();
        delay(10);
        scanNearbyEddystoneUIDs();
        delay(10);
        adv->start();
    }
}