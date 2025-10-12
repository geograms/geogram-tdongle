// src/ble/ble.cpp
// Arduino ESP32 BLE (Kolban) - continuous listening, GATT text TX, and ADV text bursts
// - ADV bursts carry Service Data whose payload begins with '>' (trigger)
// - Receiver de-duplicates identical addr|payload events within a 2000 ms sliding window

#include "ble.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include <BLEServer.h>
#include <BLE2902.h>

// ======= Config / constants =======
static const uint16_t MSG_UUID_16 = 0xFFF0;   // Service Data UUID used for ADV-text
static const size_t   ADV_TEXT_MAX = 24;      // keep whole ADV ≤ 31 bytes

// ======= ADV-text de-duplication (receiver) =======
static uint32_t g_adv_dedupe_ms = 2000;  // default dedupe window
struct SeenEntry { String key; uint32_t ts; };
static SeenEntry g_seen[128];           // small ring buffer
static uint8_t   g_seen_head = 0;

static inline bool is_adv_text_payload(const std::string& s) {
  return !s.empty() && (s[0] == '>');
}

static bool seen_recently(const String& key, uint32_t now_ms) {
  const uint8_t N = (uint8_t)(sizeof(g_seen)/sizeof(g_seen[0]));
  for (uint8_t i = 0; i < N; ++i) {
    if (g_seen[i].key.length() == 0) continue;
    if ((uint32_t)(now_ms - g_seen[i].ts) > g_adv_dedupe_ms) { g_seen[i].key = ""; continue; }
    if (g_seen[i].key == key) return true;
  }
  g_seen[g_seen_head] = { key, now_ms };
  g_seen_head = (uint8_t)((g_seen_head + 1) & (N - 1));
  return false;
}

void ble_set_adv_dedupe_window_ms(uint32_t ms) { g_adv_dedupe_ms = ms; }

// ======= Scan / listen =======
static BLEScan* g_scan = nullptr;
static bool     g_scanActive = false;

class AdvCb final : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    // Fast-path: our ADV-text in Service Data
    if (d.haveServiceData()) {
      std::string s = d.getServiceData();
      if (is_adv_text_payload(s)) {
        String key = String(s.c_str());           // payload only (ignore MAC)
        if (seen_recently(key, millis())) return; // drop duplicate within window


        Serial.print("[ADV-TEXT] ");
        for (char c : s) Serial.printf("%c", isprint((unsigned char)c) ? c : '.');
        Serial.printf("  rssi=%d  from=%s\n", d.getRSSI(), d.getAddress().toString().c_str());
        return; // handled
      }
    }

  }
};

void ble_init(const char* devName) {
  BLEDevice::init(devName && *devName ? devName : "ESP32");
}

void ble_start_listening(bool wantsDuplicates) {
  if (!g_scan) {
    g_scan = BLEDevice::getScan();
    static AdvCb cb;
    g_scan->setAdvertisedDeviceCallbacks(&cb, wantsDuplicates); // keep true; dedupe handles repeats
    g_scan->setActiveScan(true);
    g_scan->setInterval(80);
    g_scan->setWindow(60);
  }
  if (!g_scanActive) {
    g_scan->start(0, nullptr, false); // continuous until stopped
    g_scanActive = true;
    Serial.println("[BLE] Listening (continuous scan) started");
  }
}

void ble_stop_listening() {
  if (g_scan && g_scanActive) {
    g_scan->stop();
    g_scanActive = false;
    Serial.println("[BLE] Listening stopped");
  }
}

bool ble_is_listening() { return g_scanActive; }

// ======= GATT text TX (notify) =======
static BLEServer*         g_server  = nullptr;
static BLEService*        g_service = nullptr;
static BLECharacteristic* g_tx      = nullptr;

static BLEUUID SVC_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"); // NUS-like
static BLEUUID TX_UUID ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // notify

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* param) override {
    Serial.printf("[GATT] Central connected (conn_id=%u)\n", param->connect.conn_id);
  }
  void onDisconnect(BLEServer* s) override {
    Serial.println("[GATT] Central disconnected; advertising again");
    s->getAdvertising()->start();
  }
};

void ble_start_tx(const char* devName) {
  BLEDevice::init(devName && *devName ? devName : "ESP32");
  if (g_server) return;

  g_server = BLEDevice::createServer();
  static ServerCB scb; g_server->setCallbacks(&scb);

  g_service = g_server->createService(SVC_UUID);
  g_tx = g_service->createCharacteristic(
           TX_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  auto* cccd = new BLE2902(); cccd->setNotifications(true); g_tx->addDescriptor(cccd);

  g_service->start();
  BLEAdvertising* a = g_server->getAdvertising();
  a->addServiceUUID(SVC_UUID);
  a->setScanResponse(true);
  a->start();

  BLEDevice::setMTU(247); // request larger MTU (peer decides)
  Serial.println("[GATT] TX service advertising");
}

int ble_send_text(const uint8_t* data, size_t len, bool pauseDuringSend) {
  if (!g_tx) { Serial.println("[GATT] TX not ready. Call ble_start_tx() first."); return 0; }

  bool resume = false;
  if (pauseDuringSend && ble_is_listening()) { ble_stop_listening(); resume = true; }

  const size_t maxChunk = 20; // safe for default MTU=23
  int total = 0;
  for (size_t off = 0; off < len; off += maxChunk) {
    size_t n = (len - off < maxChunk) ? (len - off) : maxChunk;
    g_tx->setValue((uint8_t*)(data + off), n);
    g_tx->notify();
    total += (int)n;
    delay(5);
  }

  if (resume) ble_start_listening(true);
  return total;
}

// ======= Advertisement text burst ('>'-prefixed) =======
// Compose a Service Data (0x16) AD field with 16-bit UUID MSG_UUID_16 and a payload that starts with '>'
// Advertise it for duration_ms, then stop. If a GATT service was advertising, resume it afterwards.
void ble_send_adv_text_burst(const String& text, uint32_t duration_ms) {
  BLEAdvertising* adv = BLEDevice::getAdvertising();

  // Build payload starting with '>'
  std::string payload = text.c_str();
  if (payload.empty() || payload[0] != '>') payload.insert(payload.begin(), '>');
  if (payload.size() > ADV_TEXT_MAX) payload.resize(ADV_TEXT_MAX);

  // Prepare ADV data: Flags + Service Data (UUID 0xFFF0)
  BLEAdvertisementData advData;
  advData.setFlags(0x06); // General Discoverable, BR/EDR not supported
  BLEAdvertisementData scanResp; // empty scan response
  advData.setServiceData(BLEUUID((uint16_t)MSG_UUID_16), payload);

  const bool hadGattAdv = (g_server != nullptr);

  adv->stop();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanResp);
  adv->start();

  delay(duration_ms);

  adv->stop();

  if (hadGattAdv) {
    BLEAdvertising* a = g_server->getAdvertising();
    a->addServiceUUID(SVC_UUID);
    a->setScanResponse(true);
    a->start();
  }
}
