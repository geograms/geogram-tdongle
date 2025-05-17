#include "storage.h"
#include "pin_config.h"

#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include "USB.h"
#include "USBMSC.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

USBMSC MSC;
USBCDC USBSerial;
#define HWSerial    Serial0
#define MOUNT_POINT "/sdcard"
sdmmc_card_t *card;

#ifndef SD_CS_PIN
#define SD_CS_PIN 13
#endif

StorageManager::StorageManager() :
    _usingSD(false),
    _sdInitialized(false),
    _littleFSInitialized(false)
{
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    uint32_t count = (bufsize / card->csd.sector_size);
    sdmmc_read_sectors(card, reinterpret_cast<uint8_t *>(buffer) + offset, lba, count);
    return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    uint32_t count = (bufsize / card->csd.sector_size);
    sdmmc_write_sectors(card, buffer + offset, lba, count);
    return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    HWSerial.printf("MSC START/STOP: power: %u, start: %u, eject: %u\n", power_condition, start, load_eject);
    return true;
}

static void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == ARDUINO_USB_EVENTS) {
        arduino_usb_event_data_t *data = (arduino_usb_event_data_t *)event_data;
        switch (event_id) {
            case ARDUINO_USB_STARTED_EVENT:
                HWSerial.println("USB PLUGGED");
                break;
            case ARDUINO_USB_STOPPED_EVENT:
                HWSerial.println("USB UNPLUGGED");
                break;
            case ARDUINO_USB_SUSPEND_EVENT:
                HWSerial.printf("USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en);
                break;
            case ARDUINO_USB_RESUME_EVENT:
                HWSerial.println("USB RESUMED");
                break;
            default:
                break;
        }
    }
}

bool StorageManager::begin() {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_DDR;
    host.slot = SDMMC_HOST_SLOT_1;
    host.io_voltage = 3.3f;

    sdmmc_slot_config_t slot_config = {
        .clk = (gpio_num_t)SD_MMC_CLK_PIN,
        .cmd = (gpio_num_t)SD_MMC_CMD_PIN,
        .d0 = (gpio_num_t)SD_MMC_D0_PIN,
        .d1 = (gpio_num_t)SD_MMC_D1_PIN,
        .d2 = (gpio_num_t)SD_MMC_D2_PIN,
        .d3 = (gpio_num_t)SD_MMC_D3_PIN,
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP
    };

    gpio_set_pull_mode(slot_config.cmd, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(slot_config.d0, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(slot_config.d1, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(slot_config.d2, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(slot_config.d3, GPIO_PULLUP_ONLY);

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret == ESP_OK && card != nullptr) {
        _sdInitialized = true;
        _usingSD = true;
        Serial.println("Storage: SD card mounted via SDMMC");

        USB.onEvent(usbEventCallback);
        MSC.vendorID("ecogram");
        MSC.productID("USB");
        MSC.productRevision("1.0");
        MSC.onStartStop(onStartStop);
        MSC.onRead(onRead);
        MSC.onWrite(onWrite);
        MSC.mediaPresent(true);
        MSC.begin(card->csd.capacity, card->csd.sector_size);
        USBSerial.begin();
        USB.begin();
        return true;
    }

    if (LittleFS.begin(true)) {
        _littleFSInitialized = true;
        _usingSD = false;
        Serial.println("Storage: Using LittleFS (SD card not available)");
        return true;
    }

    Serial.println("Storage: Failed to initialize any storage system");
    return false;
}

fs::FS& StorageManager::getActiveFS() {
    if (_usingSD && _sdInitialized) {
        return *((fs::FS *)&SD);
    }
    return *((fs::FS *)&LittleFS);
}

bool StorageManager::isSDCardAvailable() const {
    return _sdInitialized;
}

bool StorageManager::isUsingSD() const {
    return _usingSD;
}

bool StorageManager::isUsingLittleFS() const {
    return !_usingSD && _littleFSInitialized;
}

File StorageManager::open(const char* path, const char* mode) {
    fs::FS& fs = getActiveFS();
    File file = fs.open(path, mode);
    if (!file) {
        Serial.printf("Failed to open file: %s\n", path);
    }
    return file;
}

bool StorageManager::exists(const char* path) {
    return getActiveFS().exists(path);
}

bool StorageManager::remove(const char* path) {
    bool success = getActiveFS().remove(path);
    if (!success) {
        Serial.printf("Failed to remove: %s\n", path);
    }
    return success;
}

bool StorageManager::mkdir(const char* path) {
    bool success = getActiveFS().mkdir(path);
    if (!success) {
        Serial.printf("Failed to create directory: %s\n", path);
    }
    return success;
}

bool StorageManager::rmdir(const char* path) {
    bool success = getActiveFS().rmdir(path);
    if (!success) {
        Serial.printf("Failed to remove directory: %s\n", path);
    }
    return success;
}

size_t StorageManager::totalBytes() {
    return _usingSD ? SD.cardSize() : LittleFS.totalBytes();
}

size_t StorageManager::usedBytes() {
    if (_usingSD) {
        uint64_t cardSize = SD.cardSize();
        uint64_t usedSize = cardSize - SD.totalBytes();
        return (size_t)usedSize;
    }
    return LittleFS.usedBytes();
}

void StorageManager::listDir(const char* dirname, uint8_t levels) {
    File root = getActiveFS().open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }

    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        root.close();
        return;
    }

    Serial.println("Directory contents:");
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels > 0) {
                listDir(file.path(), levels - 1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
    root.close();
}
