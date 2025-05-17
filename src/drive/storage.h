#pragma once

#include <FS.h>
#include <SD.h>
#include <LittleFS.h>

class StorageManager {
public:
    StorageManager();

    bool begin();                      // Initializes SD or falls back to LittleFS
    bool isSDCardAvailable() const;    // True if SD is physically detected
    bool isUsingSD() const;            // True if SD is currently used
    bool isUsingLittleFS() const;      // True if LittleFS is used

    fs::FS& getActiveFS();             // Returns the active file system
    File open(const char* path, const char* mode);
    bool exists(const char* path);
    bool remove(const char* path);
    bool mkdir(const char* path);
    bool rmdir(const char* path);
    size_t totalBytes();
    size_t usedBytes();
    void listDir(const char* dirname, uint8_t levels = 1);

private:
    bool _usingSD;
    bool _sdInitialized;
    bool _littleFSInitialized;
};
