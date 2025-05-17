#pragma once
//void startBeacon(const char* beaconName, const char* deviceId, uint16_t manufacturerId, const char* payload);
void startBeacon(const char* beaconName, const char* namespaceId, const char* instanceId);
void restartBeacon();
