# Geogram T-Dongle Firmware Releases

## Version 0.0.1

### Features
- **Device Identification**: Broadcasts as LT1-0.0.1 (LilyGo T-Dongle)
- **10-Second Ping Interval**: Broadcasts device presence every 10 seconds
- **BLE Collision Avoidance**: Random delay (0-500ms) to prevent transmission conflicts
- **Compact Device Codes**: Uses 3-letter codes for efficient BLE communication
- **Persistent Callsign**: Generates and stores random callsign (X1XXXX format)

### Installation

#### Using esptool.py (Recommended)
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 geogram-tdongle-v0.0.1.bin
```

#### Using PlatformIO
Flash the complete firmware including bootloader:
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 geogram-tdongle-v0.0.1.bin
```

### Device Specifications
- **Hardware**: LilyGo T-Dongle (ESP32-S3)
- **Flash**: 8MB
- **Display**: Built-in TFT display
- **Communication**: Bluetooth Low Energy (BLE)

### Changelog
- Initial release with device code system
- Reduced ping interval from 60s to 10s
- Added BLE collision avoidance
- Implemented compact device identification (LT1-0.0.1)
