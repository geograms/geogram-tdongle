#pragma once
/*
  ble.h — Minimal BLE “ADV text” listener + event bus for ESP32 (Arduino “Kolban” BLE).

  WHAT THIS DOES
  - Continuously scans for BLE advertisements and extracts Service Data payloads that start with '>'.
  - Single-line text (after '>') is validated (printable ASCII, min length) and de-duplicated in a sliding window.
  - Multi-parcel messages (format "AA<digits>:...") are assembled via BluetoothMessage and posted as MESSAGE_DONE.
  - Provides a tiny event bus so *any* module (e.g., LVGL UI) can subscribe and react on the main loop.
  - Optional TX: send short “ADV text bursts” in Service Data (UUID 0xFFF0) for simple device-to-device text.

  ZERO COUPLING
  - No Serial/LVGL dependencies. All notifications are events delivered by ble_tick() on the caller’s thread.
  - You can still define a weak `messageCompleted(const BluetoothMessage&)` in your app if you prefer the old hook.

  QUICK START (PlatformIO)
  1) platformio.ini:
        lib_deps =
          ESP32 BLE Arduino @ 2.0.0
  2) Code (e.g., main.cpp):
        #include "ble/ble.h"

        static void on_ble_event(const BleEvent* e, void* ctx) {
          switch (e->type) {
            case BLE_EVT_SINGLE_TEXT:
              // e->single.text (NUL-terminated), e->single.text_len, e->single.rssi, e->single.mac[6]
              // update UI here...
              break;
            case BLE_EVT_MESSAGE_DONE:
              // e->done.id[2], from[<=7], to[<=7], checksum[<=4], done.msg_len, done.snippet (truncated)
              // update UI here...
              break;
          }
        }

        void setup() {
          ble_init("ESP32");
          ble_subscribe(on_ble_event, nullptr);
          ble_start_listening(true);     // allow duplicates; we dedup internally
        }

        void loop() {
          ble_tick();                    // deliver events on the main thread
          // ... your LVGL or UI work ...
          delay(5);
        }

  SENDING A TEXT BURST
        const char* msg = ">HELLO_WORLD";
        ble_send_text((const uint8_t*)msg, strlen(msg), true);  // true = pause scan during TX

  TUNABLES (compile-time; see ble.cpp for defaults)
    - DEDUP_WINDOW_MS (default 2000)
    - MIN_SINGLE_LEN (default 5)
    - INFLIGHT_TTL_MS (default 10 minutes)
    - ADV_TEXT_MAX (default 24)
    - BLE_EVT_QUEUE_DEPTH (default 32)
    - BLE_EVT_MAX_TEXT (default 192)
    - BLE_EVT_DELIVER_BUDGET (default 12)

  RUNTIME TOOLS
    - ble_set_dedup_window(ms)
    - ble_inflight_purge_now()
    - ble_events_dropped()   // count of events dropped due to full queue
    - ble_set_logger(fn)     // optional logger hook; if set, library may emit short diagnostics

  NOTE
  - This module only inspects Service Data (UUID 0xFFF0 by default) that starts with '>'.
    It matches what ble_send_text() emits and avoids parsing random manufacturer data.

  Copyright:
  - MIT-like: copy/paste freely into your projects.
*/

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Event model ----------
typedef enum {
  BLE_EVT_NONE = 0,
  BLE_EVT_SINGLE_TEXT = 1,     // plain '>' single-line text (validated, deduped)
  BLE_EVT_MESSAGE_DONE = 2,    // multi-parcel assembled via BluetoothMessage
  BLE_EVT_SCAN_STARTED = 3,    // optional future use
  BLE_EVT_SCAN_STOPPED = 4,    // optional future use
  BLE_EVT_TX_SENT      = 5     // optional future use
} BleEventType;

#ifndef BLE_EVT_MAX_TEXT
#define BLE_EVT_MAX_TEXT 192    // bytes for text/snippet buffers (NUL included)
#endif

typedef struct {
  char     text[BLE_EVT_MAX_TEXT]; // NUL-terminated, truncated if needed (includes leading '>')
  uint16_t text_len;               // length not including the trailing NUL
  int8_t   rssi;
  uint8_t  mac[6];                 // advertiser address (bytes)
} BleEvtSingleText;

typedef struct {
  char     id[3];                  // 2-char ID (e.g., "AA"), plus NUL
  char     from[8];                // truncated sender ID + NUL
  char     to[8];                  // truncated destination ID + NUL
  char     checksum[5];            // truncated checksum + NUL
  uint32_t msg_len;                // full message length
  char     snippet[BLE_EVT_MAX_TEXT]; // truncated preview; NUL-terminated
} BleEvtMessageDone;

typedef struct {
  uint8_t type;  // BleEventType
  union {
    BleEvtSingleText  single;
    BleEvtMessageDone done;
  } data;
} BleEvent;

// Subscriber signature (kept light/ABI-friendly)
typedef void (*BleEventCb)(const BleEvent* e, void* user_ctx);

// ---------- Public API ----------
void ble_init(const char* devName);
void ble_start_listening(bool wantsDuplicates);
void ble_stop_listening();
bool ble_is_listening();

int  ble_send_text(const uint8_t* data, size_t len, bool pauseDuringSend);

// Event bus
int  ble_subscribe(BleEventCb cb, void* user_ctx); // returns token >=1 on success, 0 on failure
void ble_unsubscribe(int token);
void ble_tick(void);

// Optional tools
void ble_set_dedup_window(uint32_t ms);
void ble_inflight_purge_now(void);
uint32_t ble_events_dropped(void);

// Optional logger (no Serial inside lib; app can wire a logger)
void ble_set_logger(void (*logger)(const char* line));

// Back-compat alias (if you used older name)
void ble_set_adv_dedupe_window_ms(uint32_t ms);

#ifdef __cplusplus
} // extern "C"
#endif

// Optional weak hook preserved for legacy users.
// Define this in your app if you want a direct callback when a message completes.
extern "C" void messageCompleted(const struct BluetoothMessage& msg) __attribute__((weak));
