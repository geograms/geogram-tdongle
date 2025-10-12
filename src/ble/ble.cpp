// src/ble/ble.cpp
// ESP32 BLE (Kolban) - continuous listening for ADV Service-Data text ('>'),
// single-line print with length+ASCII checks, parcel reassembly via BluetoothMessage,
// dedup within a sliding window, and short ADV burst TX.

#include "ble.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>
#include "bluetoothmessage.h"
#include <new> // placement new

// ======= Config / constants =======
static const uint16_t MSG_UUID_16 = 0xFFF0;   // Service Data UUID used for ADV text
#ifndef ADV_TEXT_MAX
#define ADV_TEXT_MAX 24                       // keep whole ADV ≤ 31 bytes
#endif
#ifndef DEDUP_WINDOW_MS
#define DEDUP_WINDOW_MS 2000                  // 2s dedupe window
#endif
#ifndef MIN_SINGLE_LEN
#define MIN_SINGLE_LEN 5                      // min content length after '>'
#endif
#ifndef INFLIGHT_TTL_MS
#define INFLIGHT_TTL_MS (10UL * 60UL * 1000UL) // 10 minutes for incomplete assemblies
#endif

// ======= Utility =======
static inline bool is_printable_ascii(const char* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)p[i];
    if (c < 32 || c > 126) return false;
  }
  return true;
}

static inline bool is_parcel_like(const String& s) {
  // "AA<digits>:"
  if (s.length() < 4) return false;
  char c0 = s[0], c1 = s[1];
  if (!(c0 >= 'A' && c0 <= 'Z' && c1 >= 'A' && c1 <= 'Z')) return false;
  int i = 2, n = (int)s.length();
  if (i >= n || !(s[i] >= '0' && s[i] <= '9')) return false;
  while (i < n && (s[i] >= '0' && s[i] <= '9')) ++i;
  return (i < n && s[i] == ':');
}

// ======= De-duplication (payload-only; ignore MAC) =======
static uint32_t g_dedupe_ms = DEDUP_WINDOW_MS;
struct SeenEntry { String key; uint32_t ts; };
static SeenEntry g_seen[128];  // power-of-two sized ring
static uint8_t   g_seen_head = 0;

static bool seen_recently_payload(const String& key, uint32_t now_ms) {
  const uint8_t N = (uint8_t)(sizeof(g_seen)/sizeof(g_seen[0]));
  for (uint8_t i = 0; i < N; ++i) {
    if (g_seen[i].key.length() == 0) continue;
    if ((uint32_t)(now_ms - g_seen[i].ts) > g_dedupe_ms) { g_seen[i].key = ""; continue; }
    if (g_seen[i].key == key) return true;
  }
  g_seen[g_seen_head] = { key, now_ms };
  g_seen_head = (uint8_t)((g_seen_head + 1) & (N - 1));
  return false;
}

// ======= In-flight assembly slots (AA..ZZ → 26*26) =======
struct Inflight {
  BluetoothMessage bm;
  uint32_t lastTouchMs = 0;
};
static Inflight g_inflight[26*26];

static inline int inflight_index(const String& id2) {
  if (id2.length() < 2) return -1;
  char a = id2[0], b = id2[1];
  if (a<'A'||a>'Z'||b<'A'||b>'Z') return -1;
  return (a-'A')*26 + (b-'A');
}

static void inflight_reset(Inflight& slot) {
  // Reconstruct BluetoothMessage in place (no move/assign required)
  new (&slot.bm) BluetoothMessage();
  slot.lastTouchMs = 0;
}

static void inflight_sweep(uint32_t now) {
  for (auto &slot : g_inflight) {
    if (slot.lastTouchMs == 0) continue;
    if (!slot.bm.isMessageCompleted() && (uint32_t)(now - slot.lastTouchMs) >= INFLIGHT_TTL_MS) {
      inflight_reset(slot);
    }
  }
}

// ======= Completion hook (weak) =======
extern "C" __attribute__((weak))
void messageCompleted(const BluetoothMessage& msg) {
  Serial.printf("[BLE] Completed id=%s from=%s to=%s len=%u checksum=%s\n",
    msg.getId().c_str(), msg.getIdFromSender().c_str(), msg.getIdDestination().c_str(),
    (unsigned)msg.getMessage().length(), msg.getChecksum().c_str());
  Serial.printf("[BLE] Text: %s\n", msg.getMessage().c_str());
}

// ======= Scan / listen =======
static BLEScan* g_scan = nullptr;
static bool     g_scanActive = false;

class AdvCb final : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    // Only look at Service Data; ADV bursts we send use UUID 0xFFF0 with '>' payload
    if (!d.haveServiceData()) return;

    std::string sd = d.getServiceData();
    if (sd.empty() || sd[0] != '>') return;  // must start with '>'

    const char* bytes = sd.data();
    size_t total = sd.size();
    if (total < 2) return;

    const char* content = bytes + 1;
    size_t      clen    = total - 1;

    if (!is_printable_ascii(content, clen)) return;
    if ((int)clen < MIN_SINGLE_LEN) return;

    String payload = String(sd.c_str());   // ">" + printable content

    // Drop duplicates (payload-only)
    if (seen_recently_payload(payload, millis())) return;

    // Print validated unique payload
    Serial.printf("[ADV-TEXT] %s  rssi=%d  from=%s\n",
                  payload.c_str(), d.getRSSI(), d.getAddress().toString().c_str());

    // Assemble if it looks like a parcel ("AA<digits>:...")
    String body = payload.substring(1);
    if (is_parcel_like(body)) {
      int idx = inflight_index(body.substring(0, 2));
      if (idx >= 0) {
        Inflight &slot = g_inflight[idx];
        slot.bm.addMessageParcel(body);
        slot.lastTouchMs = millis();
        if (slot.bm.isMessageCompleted()) {
          messageCompleted(slot.bm);
          inflight_reset(slot);
        }
      }
    }

    inflight_sweep(millis());
  }
};

void ble_init(const char* devName) {
  BLEDevice::init(devName && *devName ? devName : "ESP32");
}

void ble_start_listening(bool wantsDuplicates) {
  if (!g_scan) {
    g_scan = BLEDevice::getScan();
    static AdvCb cb;
    g_scan->setAdvertisedDeviceCallbacks(&cb, wantsDuplicates);
    g_scan->setActiveScan(true);
    g_scan->setInterval(80);
    g_scan->setWindow(60);
  }
  if (!g_scanActive) {
    // Continuous until stopped; non-blocking
    g_scan->start(0, nullptr, false);
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

// ======= ADV text burst ('>'-prefixed Service Data) =======
static void adv_send_text_burst(const String& text, uint32_t duration_ms) {
  BLEAdvertising* adv = BLEDevice::getAdvertising();

  std::string payload = text.c_str();
  if (payload.empty() || payload[0] != '>') payload.insert(payload.begin(), '>');
  if (payload.size() > ADV_TEXT_MAX) payload.resize(ADV_TEXT_MAX);

  BLEAdvertisementData advData;
  advData.setFlags(0x06); // General Discoverable, BR/EDR not supported
  BLEAdvertisementData scanResp; // empty scan response
  advData.setServiceData(BLEUUID((uint16_t)MSG_UUID_16), payload);

  adv->stop();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanResp);
  adv->start();

  delay(duration_ms);

  adv->stop();
}

// Public API
int ble_send_text(const uint8_t* data, size_t len, bool pauseDuringSend) {
  if (!data || len == 0) return 0;

  bool resume = false;
  if (pauseDuringSend && ble_is_listening()) { ble_stop_listening(); resume = true; }

  String txt; txt.reserve(len + 1); txt += '>';
  for (size_t i = 0; i < len; ++i) txt += (char)data[i];

  adv_send_text_burst(txt, 100); // ~100 ms burst

  if (resume) ble_start_listening(true);
  return (int)len;
}

// Optional tuning / maintenance
void ble_set_dedup_window(uint32_t ms) { g_dedupe_ms = ms ? ms : 1; }
void ble_inflight_purge_now() { inflight_sweep(millis() + INFLIGHT_TTL_MS + 1); }
void ble_tick() { /* no-op; kept for compatibility */ }

// Back-compat alias (if used elsewhere)
extern "C" void ble_set_adv_dedupe_window_ms(uint32_t ms) { ble_set_dedup_window(ms); }
