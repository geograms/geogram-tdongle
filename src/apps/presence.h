#pragma once

#include <Arduino.h>

/**
 * @brief Update presence data for a given device ID at a specific timestamp.
 * 
 * @param deviceId Identifier of the remote BLE device.
 * @param timestamp The current timestamp in seconds since epoch.
 */
void updatePresence(const String& deviceId, time_t timestamp);

/**
 * @brief Count the number of minutes a device was detected between two timestamps.
 * 
 * @param deviceId Identifier of the device to query.
 * @param start Start of the time range (inclusive).
 * @param end End of the time range (inclusive).
 * @return int Number of minutes where the device was detected within range.
 */
int countPresenceMinutes(const String& deviceId, time_t start, time_t end);
