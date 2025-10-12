#pragma once
#include <Arduino.h>

// Listening (scan)
void ble_init(const char* devName);
void ble_start_listening(bool wantsDuplicates = true);
void ble_stop_listening();
bool ble_is_listening();

// TX (GATT notify)
void ble_start_tx(const char* devName = "ESP32");
int  ble_send_text(const uint8_t* data, size_t len, bool pauseDuringSend);

// Convenience overload
inline int ble_send_text(const String& s, bool pauseDuringSend) {
  return ble_send_text(reinterpret_cast<const uint8_t*>(s.c_str()),
                       static_cast<size_t>(s.length()),
                       pauseDuringSend);
}

// Advertisement text burst (payload starts with ':')
void ble_send_adv_text_burst(const String& text, uint32_t duration_ms);

// Send short text as an advertisement burst (default 100 ms)
void ble_send_adv_text_burst(const String& text, uint32_t duration_ms = 100);

// Configure duplicate-suppression window for ADV-text (default 100 ms)
void ble_set_adv_dedupe_window_ms(uint32_t ms);