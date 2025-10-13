// ESP32 BLE (Kolban) — ADV '>' text listener + event bus + assembler + ADV-burst TX
#include "ble.h"

#include <Arduino.h>
#include <string.h>
#include <new>                 // placement new

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <BLEScan.h>

#include "bluetoothmessage.h"

// ---------- Tunables ----------
static const uint16_t MSG_UUID_16 = 0xFFF0;   // Service Data UUID for ADV text

#ifndef ADV_TEXT_MAX
#define ADV_TEXT_MAX 24                        // keep whole ADV ≤ 31 bytes
#endif
#ifndef DEDUP_WINDOW_MS
#define DEDUP_WINDOW_MS 2000                   // 2s dedupe window
#endif
#ifndef MIN_SINGLE_LEN
#define MIN_SINGLE_LEN 5                       // minimum length after '>'
#endif
#ifndef INFLIGHT_TTL_MS
#define INFLIGHT_TTL_MS (10UL * 60UL * 1000UL) // 10 minutes assembler timeout
#endif
#ifndef BLE_EVT_QUEUE_DEPTH
#define BLE_EVT_QUEUE_DEPTH 32                 // ring depth (events)
#endif
#ifndef BLE_EVT_DELIVER_BUDGET
#define BLE_EVT_DELIVER_BUDGET 12              // max events per ble_tick() call
#endif

// ---------- Optional logger ----------
static void (*g_logger)(const char* line) = nullptr;
static inline void log_line(const char* s) { if (g_logger) g_logger(s); }
static void logf(const char* fmt, ...) {
  if (!g_logger) return;
  char buf[160];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  g_logger(buf);
}

// ---------- Small utils ----------
static inline bool is_printable_ascii(const char* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)p[i];
    if (c < 32 || c > 126) return false;
  }
  return true;
}

static inline bool is_parcel_like(const char* s, size_t n) {
  if (n < 4) return false;
  char c0 = s[0], c1 = s[1];
  if (!(c0 >= 'A' && c0 <= 'Z' && c1 >= 'A' && c1 <= 'Z')) return false;
  size_t i = 2;
  if (i >= n || !(s[i] >= '0' && s[i] <= '9')) return false;
  while (i < n && (s[i] >= '0' && s[i] <= '9')) ++i;
  return (i < n && s[i] == ':');
}

// ---------- Dedupe (payload-only; ignore MAC) ----------
static uint32_t g_dedupe_ms = DEDUP_WINDOW_MS;
struct SeenEntry { String key; uint32_t ts; };
static SeenEntry g_seen[128];
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

// ---------- In-flight assembler (AA..ZZ → 26*26) ----------
struct Inflight {
  BluetoothMessage bm;
  uint32_t lastTouchMs = 0;
};
static Inflight g_inflight[26*26];

static inline int inflight_index_2(const char* id2) {
  char a = id2[0], b = id2[1];
  if (a<'A'||a>'Z'||b<'A'||b>'Z') return -1;
  return (a-'A')*26 + (b-'A');
}

static void inflight_reset(Inflight& slot) {
  new (&slot.bm) BluetoothMessage();   // placement-new; no move/assign needed
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

// ---------- Event bus (ring + subscribers) ----------
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static BleEvent g_evt_q[BLE_EVT_QUEUE_DEPTH];
static volatile uint16_t g_evt_head = 0; // write index
static volatile uint16_t g_evt_tail = 0; // read index
static uint32_t g_evt_dropped = 0;

static portMUX_TYPE g_evt_mux = portMUX_INITIALIZER_UNLOCKED;

#define Q_NEXT(i) ((uint16_t)((i + 1) % BLE_EVT_QUEUE_DEPTH))
static inline bool q_full() { return Q_NEXT(g_evt_head) == g_evt_tail; }
static inline bool q_empty(){ return g_evt_head == g_evt_tail; }

static void q_push(const BleEvent* e) {
  portENTER_CRITICAL(&g_evt_mux);
  if (q_full()) {
    // drop oldest
    g_evt_tail = Q_NEXT(g_evt_tail);
    ++g_evt_dropped;
  }
  g_evt_q[g_evt_head] = *e;  // POD copy
  g_evt_head = Q_NEXT(g_evt_head);
  portEXIT_CRITICAL(&g_evt_mux);
}

static bool q_pop(BleEvent* out) {
  bool ok = false;
  portENTER_CRITICAL(&g_evt_mux);
  if (!q_empty()) {
    *out = g_evt_q[g_evt_tail];
    g_evt_tail = Q_NEXT(g_evt_tail);
    ok = true;
  }
  portEXIT_CRITICAL(&g_evt_mux);
  return ok;
}

#ifndef BLE_MAX_SUBSCRIBERS
#define BLE_MAX_SUBSCRIBERS 4
#endif

typedef struct { BleEventCb cb; void* ctx; uint8_t used; } Sub;
static Sub g_subs[BLE_MAX_SUBSCRIBERS];

int ble_subscribe(BleEventCb cb, void* user_ctx) {
  if (!cb) return 0;
  for (int i = 0; i < BLE_MAX_SUBSCRIBERS; ++i) {
    if (!g_subs[i].used) {
      g_subs[i].cb = cb;
      g_subs[i].ctx = user_ctx;
      g_subs[i].used = 1;
      return i + 1; // token
    }
  }
  return 0;
}

static inline void mac_to_bytes(const BLEAddress& addr, uint8_t out[6]) {
  BLEAddress tmp = addr;                 // getNative() is non-const
  esp_bd_addr_t* native = tmp.getNative();  // uint8_t[6]
  memcpy(out, native, 6);
}


void ble_unsubscribe(int token) {
  if (token <= 0) return;
  int idx = token - 1;
  if (idx >= 0 && idx < BLE_MAX_SUBSCRIBERS) {
    g_subs[idx].used = 0;
    g_subs[idx].cb = nullptr;
    g_subs[idx].ctx = nullptr;
  }
}

void ble_tick(void) {
  // Deliver up to BLE_EVT_DELIVER_BUDGET events per call
  BleEvent e;
  int budget = BLE_EVT_DELIVER_BUDGET;
  while (budget-- > 0 && q_pop(&e)) {
    for (int i = 0; i < BLE_MAX_SUBSCRIBERS; ++i) {
      if (g_subs[i].used && g_subs[i].cb) {
        g_subs[i].cb(&e, g_subs[i].ctx);
      }
    }
  }
}

// ---------- Scan / listen ----------
static BLEScan* g_scan = nullptr;
static bool     g_scanActive = false;

class AdvCb final : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
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

    uint32_t now = millis();

    // Dedup by payload only (ignore MAC)
    String payload = String(sd.c_str()); // '>' + text
    if (seen_recently_payload(payload, now)) return;

    // Post SINGLE_TEXT event
    BleEvent ev = {};
    ev.type = BLE_EVT_SINGLE_TEXT;
    // copy text (payload including leading '>')
    size_t copyN = (payload.length() < (BLE_EVT_MAX_TEXT - 1)) ? payload.length() : (BLE_EVT_MAX_TEXT - 1);
    memcpy(ev.data.single.text, payload.c_str(), copyN);
    ev.data.single.text[copyN] = '\0';
    ev.data.single.text_len = (uint16_t)copyN;
    ev.data.single.rssi = (int8_t)d.getRSSI();
    mac_to_bytes(d.getAddress(), ev.data.single.mac);
    q_push(&ev);

    // If looks like parcel, feed assembler
    if (is_parcel_like(content, clen)) {
      // id2 = first two chars
      int idx = inflight_index_2(content);
      if (idx >= 0) {
        Inflight &slot = g_inflight[idx];
        // body as Arduino String for BluetoothMessage API
        String body = String(content);  // "AA<digits>:..."
        slot.bm.addMessageParcel(body);
        slot.lastTouchMs = now;
        if (slot.bm.isMessageCompleted()) {
          // Compose MESSAGE_DONE event with truncated fields
          BleEvent ev2 = {};
          ev2.type = BLE_EVT_MESSAGE_DONE;
          // id 2-chars
          ev2.data.done.id[0] = content[0];
          ev2.data.done.id[1] = content[1];
          ev2.data.done.id[2] = '\0';
          // from/to/checksum/message
          String from = slot.bm.getIdFromSender();
          String to   = slot.bm.getIdDestination();
          String ck   = slot.bm.getChecksum();
          String msg  = slot.bm.getMessage();

          // Truncate safely
          strncpy(ev2.data.done.from, from.c_str(), sizeof(ev2.data.done.from)-1);
          ev2.data.done.from[sizeof(ev2.data.done.from)-1] = '\0';
          strncpy(ev2.data.done.to, to.c_str(), sizeof(ev2.data.done.to)-1);
          ev2.data.done.to[sizeof(ev2.data.done.to)-1] = '\0';
          strncpy(ev2.data.done.checksum, ck.c_str(), sizeof(ev2.data.done.checksum)-1);
          ev2.data.done.checksum[sizeof(ev2.data.done.checksum)-1] = '\0';

          ev2.data.done.msg_len = (uint32_t)msg.length();
          size_t sN = (msg.length() < (BLE_EVT_MAX_TEXT - 1)) ? msg.length() : (BLE_EVT_MAX_TEXT - 1);
          memcpy(ev2.data.done.snippet, msg.c_str(), sN);
          ev2.data.done.snippet[sN] = '\0';

          q_push(&ev2);

          // Legacy weak hook (optional)
          messageCompleted(slot.bm);

          // Clear slot
          inflight_reset(slot);
        }
      }
    }

    // Maintenance
    inflight_sweep(now);
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
    // Continuous, non-blocking scan (Kolban BLE)
    g_scan->start(0, nullptr, false);
    g_scanActive = true;
    log_line("[BLE] Listening (continuous scan) started");
  }
}

void ble_stop_listening() {
  if (g_scan && g_scanActive) {
    g_scan->stop();
    g_scanActive = false;
    log_line("[BLE] Listening stopped");
  }
}

bool ble_is_listening() { return g_scanActive; }

// ---------- ADV text burst TX ----------
static void adv_send_text_burst(const String& text, uint32_t duration_ms) {
  BLEAdvertising* adv = BLEDevice::getAdvertising();

  std::string payload = text.c_str();
  if (payload.empty() || payload[0] != '>') payload.insert(payload.begin(), '>');
  if (payload.size() > ADV_TEXT_MAX) payload.resize(ADV_TEXT_MAX);

  BLEAdvertisementData advData;
  advData.setFlags(0x06); // General Discoverable, BR/EDR not supported
  BLEAdvertisementData scanResp; // empty
  advData.setServiceData(BLEUUID((uint16_t)MSG_UUID_16), payload);

  adv->stop();
  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanResp);
  adv->start();

  delay(duration_ms);

  adv->stop();
}

int ble_send_text(const uint8_t* data, size_t len, bool pauseDuringSend) {
  if (!data || len == 0) return 0;

  bool resume = false;
  if (pauseDuringSend && ble_is_listening()) { ble_stop_listening(); resume = true; }

  String txt; txt.reserve(len + 1); txt += '>';
  for (size_t i = 0; i < len; ++i) txt += (char)data[i];

  adv_send_text_burst(txt, 100); // ~100 ms

  if (resume) ble_start_listening(true);
  return (int)len;
}

// ---------- Tools ----------
void ble_set_dedup_window(uint32_t ms) { g_dedupe_ms = ms ? ms : 1; }
void ble_inflight_purge_now() { inflight_sweep(millis() + INFLIGHT_TTL_MS + 1); }
uint32_t ble_events_dropped(void) { return g_evt_dropped; }
void ble_set_logger(void (*logger)(const char* line)) { g_logger = logger; }
void ble_set_adv_dedupe_window_ms(uint32_t ms) { ble_set_dedup_window(ms); }

// ---------- Legacy weak (provided in header) ----------
extern "C" void messageCompleted(const BluetoothMessage& msg) __attribute__((weak));
