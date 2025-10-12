// src/ble/bluetoothmessage.cpp
#include <Arduino.h>
#include "bluetoothmessage.h"
#include <algorithm>
#include <map>
#include <vector>

// ---- BluetoothMessage implementation ----

BluetoothMessage::BluetoothMessage()
  : messageCompleted(false),
    id(""), idFromSender(""),
    idDestination(""), message(""),
    checksum(""),
    timeStamp(currentMillis64()) {}

BluetoothMessage::BluetoothMessage(const String& idFromSender_,
                                   const String& idDestination_,
                                   const String& messageToSend,
                                   bool singleMessage)
  : messageCompleted(false),
    id(generateRandomId()),
    idFromSender(idFromSender_),
    idDestination(idDestination_),
    message(messageToSend),
    checksum(calculateChecksum(messageToSend)),
    timeStamp(currentMillis64()) {
  if (singleMessage) {
    messageBox.insert({"000", messageToSend});
  } else {
    splitDataIntoParcels();
  }
}

std::vector<String> BluetoothMessage::getMessageParcels() const {
  std::vector<String> out;
  out.reserve(messageBox.size());
  for (auto& kv : messageBox) out.push_back(kv.second);
  return out;
}

String BluetoothMessage::getOutput() const {
  if (messageBox.empty()) return String();
  String out;
  for (auto& kv : messageBox) { out += kv.second; out += " | "; }
  if (out.length() >= 3) out.remove(out.length() - 3);
  return out;
}

void BluetoothMessage::addMessageParcel(const String& messageParcel) {
  if (messageCompleted) return;

  // single command (no ':')
  if (isSingleCommand(messageParcel)) {
    message = messageParcel;
    messageBox.insert({"000", messageParcel});
    messageCompleted = true;
    return;
  }

  int colon = messageParcel.indexOf(':');
  if (colon < 0) return;

  String parcelId = messageParcel.substring(0, colon);
  if (messageBox.find(parcelId) != messageBox.end()) return; // de-dupe
  messageBox.insert({parcelId, messageParcel});

  int index = -1;
  if (parcelId.length() >= 3) {
    String idxStr = parcelId.substring(2);
    index = idxStr.toInt();
    if (id.length() == 0) id = parcelId.substring(0, 2);
  } else {
    BTM_LOGI("Invalid parcel ID: %s", parcelId.c_str());
    return;
  }
  if (index < 0) return;

  if (index == 0) {
    // "<id>0:<from>:<dest>:<checksum>"
    int p1 = messageParcel.indexOf(':');
    int p2 = messageParcel.indexOf(':', p1 + 1);
    int p3 = messageParcel.indexOf(':', p2 + 1);
    if (p1 > 0 && p2 > p1 && p3 > p2) {
      idFromSender  = messageParcel.substring(p1 + 1, p2);
      idDestination = messageParcel.substring(p2 + 1, p3);
      id            = parcelId.substring(0, 2);
      checksum      = messageParcel.substring(p3 + 1);
    }
    return;
  }

  if ((int)messageBox.size() < 2) return;   // need header + one data
  if (checksum.length() == 0) return;       // wait for header

  // reassemble: header then data1..N in key order
  String result;
  for (auto& kv : messageBox) {
    const String& key = kv.first;
    if (key.length() >= 3) {
      int idx = key.substring(2).toInt();
      if (idx >= 1) {
        const String& full = kv.second;
        int a = full.indexOf(':');
        if (a >= 0) result += full.substring(a + 1);
      }
    }
  }

  String cur = calculateChecksum(result);
  if (cur == checksum) {
    message = result;
    messageCompleted = true;
  }
}

String BluetoothMessage::getFirstMissingParcel() const {
  if (checksum.length() == 0) return idPrefix() + String("0");
  if (messageBox.size() == 1) return idPrefix() + String("1");
  int boxSize = (int)messageBox.size();
  for (int i = 0; i < boxSize; ++i) {
    String k = idPrefix() + String(i);
    if (messageBox.find(k) == messageBox.end()) return k;
  }
  return idPrefix() + String(boxSize);
}

std::vector<String> BluetoothMessage::getMissingParcels() const {
  std::vector<String> missing;
  if (messageBox.empty()) return missing;
  int maxSeen = -1;
  for (auto& kv : messageBox) {
    const String& k = kv.first;
    if (k.length() < 3) continue;
    int idx = k.substring(2).toInt();
    if (idx > maxSeen) maxSeen = idx;
  }
  if (maxSeen <= 0) return missing;
  String base = idPrefix();
  for (int i = 0; i < maxSeen; ++i) {
    String key = base + String(i);
    if (messageBox.find(key) == messageBox.end()) missing.push_back(key);
  }
  return missing;
}

// ---------- private helpers ----------

uint64_t BluetoothMessage::currentMillis64() {
  return (uint64_t)millis(); // widen to 64 bits; wrap handled by caller if needed
}

bool BluetoothMessage::isSingleCommand(const String& s) {
  return (s.length() > 0) && (s.indexOf(':') < 0);
}

void BluetoothMessage::splitByColon(const String& src, std::vector<String>& out, int maxParts) {
  out.clear();
  int start = 0, parts = 0;
  while (parts + 1 < maxParts) {
    int p = src.indexOf(':', start);
    if (p < 0) break;
    out.push_back(src.substring(start, p));
    start = p + 1;
    ++parts;
  }
  out.push_back(src.substring(start));
}

String BluetoothMessage::idPrefix() const {
  if (id.length() >= 2) return id.substring(0,2);
  if (!messageBox.empty()) {
    auto it = messageBox.begin();
    if (it->first.length() >= 2) return it->first.substring(0,2);
  }
  return String("AA");
}

String BluetoothMessage::calculateChecksum(const String& data) const {
  if (data.length() == 0) return String("AAAA");
  long sum = 0;
  const char* p = data.c_str();
  for (size_t i = 0; i < data.length(); ++i) sum += (unsigned char)p[i];
  char cs[5];
  for (int i = 0; i < 4; ++i) { cs[i] = char('A' + (sum % 26)); sum /= 26; }
  cs[4] = '\0';
  return String(cs);
}

String BluetoothMessage::generateRandomId() {
  char first  = char('A' + (int)random(0, 26));
  char second = char('A' + (int)random(0, 26));
  char buf[3] = { first, second, '\0' };
  return String(buf);
}

void BluetoothMessage::splitDataIntoParcels() {
  int dataLength = message.length();
  int parcels = (dataLength + TEXT_LENGTH_PER_PARCEL - 1) / TEXT_LENGTH_PER_PARCEL;

  // header index=0
  String uidHeader = id + "0";
  String header = uidHeader + ":" + idFromSender + ":" + idDestination + ":" + checksum;
  messageBox.insert({uidHeader, header});

  for (int i = 0; i < parcels; ++i) {
    int start = i * TEXT_LENGTH_PER_PARCEL;
    int end   = std::min(start + TEXT_LENGTH_PER_PARCEL, dataLength);
    String text = message.substring(start, end);
    int value = i + 1;
    String uid = id + String(value);
    String payload = uid + ":" + text;
    messageBox.insert({uid, payload});
  }
}
