# Geogram T-Dongle S3 Firmware

Custom firmware for the [LilyGO T-Dongle S3](https://www.lilygo.cc/products/t-dongle-s3), enabling it to function as a **BLE beacon** for integration with the **Geogram Android app**. The device advertises presence and metadata, enabling proximity detection, logging, and user awareness in a fully offline-first radio-aware environment.

![T-Dongle](docs/t-dongle.png)

---

## 📡 Features

- 📶 **BLE Beacon Broadcasting**  
  Advertises presence packets over BLE containing a unique node ID.

- 🔁 **Geogram Android App Integration**  
  Recognized by the app for presence detection and location-free tracking.

- 💡 **OLED Status Display**  
  Displays callsign, battery voltage, interval timer, and BLE state using onboard 0.91" OLED.

- 🔋 **Battery Voltage Monitoring**  
  Reads VBAT from internal ADC and displays real-time voltage on screen.

- 🔧 **USB Serial Debugging**  
  Console logs at 115200 baud for diagnostics and development.

- 🔄 **Configurable Interval**  
  Default beacon interval is 5 seconds (modifiable in code).

---

## 🧱 Hardware Requirements

- **LilyGO T-Dongle S3**
  - ESP32-S3 microcontroller  
  - BLE + WiFi  
  - 0.91" I²C OLED display  
  - JST battery connector with charging  
  - USB-C for power and programming

---

## 🔧 Build & Flash Instructions

This project uses [PlatformIO](https://platformio.org/). Clone the repository and flash with:

```bash
git clone https://github.com/geograms/tdongle-firmware.git
cd tdongle-firmware
pio run --target upload
```

> Board: `esp32-s3-devkitc`  
> Upload via USB-C. No bootloader button needed unless flashing fails.

### Serial Monitor

```bash
pio device monitor -b 115200
```

---

## 📄 License

Licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)

---

## 📣 Support

- Discussions: https://github.com/orgs/geograms/discussions  
- Issues: https://github.com/geograms/geogram-html/issues

---

## 🤝 Contributors

See [`CONTRIBUTORS.md`](https://github.com/geograms/geogram-html/blob/main/CONTRIBUTORS.md)

**Primary Contributor**  
👤 Max Brito (Portugal/Germany) — 2025–present  
- BLE protocol, OLED interface, ESP32 power management, Android integration
