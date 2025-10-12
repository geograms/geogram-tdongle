#pragma once
// src/ble/ble.h
//
// Minimal BLE facade used by the app. Matches ble.cpp.
//
// - Non-blocking TX bursts (finish in ble_tick()).
// - Continuous scanning when listening is enabled.
// - Single-line dedup window configurable (default set in ble.cpp; override via ble_set_dedup_window).

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Forward declaration to avoid including bluetoothmessage.h here.
class BluetoothMessage;

#ifdef __cplusplus
extern "C" {
#endif

// ---- Lifecycle ----
void ble_init(const char* deviceName);
void ble_start_listening(bool restartScanner);
void ble_stop_listening();
bool ble_is_listening();

// ---- TX (non-blocking) ----
// Starts a short advertising burst with payload ">...". If pauseDuringSend==true,
// listening is paused during the burst and resumed automatically after it ends in ble_tick().
// Returns 1 on started, 0 if not started (e.g., busy or invalid args).
int  ble_send_text(const uint8_t* data, size_t len, bool pauseDuringSend);

// ---- Periodic maintenance ----
// Call frequently from your main loop (e.g., right after LVGL tick).
void ble_tick();

// ---- Tuning / maintenance ----
void ble_set_dedup_window(uint32_t ms); // single-line dedup window
void ble_inflight_purge_now();          // manual purge of stale partial messages

// ---- Completion hook ----
// You can define this in your code to override the weak default in ble.cpp.
void messageCompleted(const BluetoothMessage& msg);

#ifdef __cplusplus
} // extern "C"
#endif
