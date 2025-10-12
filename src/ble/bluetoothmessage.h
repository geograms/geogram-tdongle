#pragma once
// bluetoothmessage.h
// Compact C++ port of your BluetoothMessage (Arduino/ESP32 friendly)

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include <string>
  // Minimal Arduino String substitute for host/unit tests (optional).
  // In firmware builds you already have <Arduino.h>.
  #define String std::string
  #define millis() 0
#endif

#include <map>
#include <vector>
#include <algorithm>

// Max text chars per parcel (data chunk). Adjust at build time with -DTEXT_LENGTH_PER_PARCEL=...
#ifndef TEXT_LENGTH_PER_PARCEL
#define TEXT_LENGTH_PER_PARCEL 20
#endif

// Optional logging macro (leave empty to silence)
#ifndef BTM_LOGI
  #define BTM_LOGI(...) do{}while(0)
#endif

// std::map comparator for Arduino String (lexicographic)
struct StringLess {
  bool operator()(const String& a, const String& b) const {
  #ifdef ARDUINO
    return a.compareTo(b) < 0;
  #else
    return a < b;
  #endif
  }
};

class BluetoothMessage {
public:
  // Constructors
  BluetoothMessage();
  BluetoothMessage(const String& idFromSender,
                   const String& idDestination,
                   const String& messageToSend,
                   bool singleMessage);

  // Accessors
  String   getChecksum()      const { return checksum; }
  String   getId()            const { return id; }
  String   getIdDestination() const { return idDestination; }
  String   getIdFromSender()  const { return idFromSender; }
  String   getMessage()       const { return message; }
  bool     isMessageCompleted() const { return messageCompleted; }
  uint64_t getTimeStamp()     const { return timeStamp; }
  String   getAuthor()        const { return idFromSender; }

  void setMessageCompleted(bool v)     { messageCompleted = v; }
  void setId(const String& v)          { id = v; }
  void setIdFromSender(const String& v){ idFromSender = v; }
  void setIdDestination(const String& v){ idDestination = v; }
  void setMessage(const String& v)     { message = v; }
  void setChecksum(const String& v)    { checksum = v; }

  // Parcels
  int getMessageParcelsTotal() const { return (int)messageBox.size(); }
  std::vector<String> getMessageParcels() const; // values in key order
  const std::map<String,String,StringLess>& getMessageBox() const { return messageBox; }
  String getOutput() const; // human-friendly dump

  // Feeding / reassembly
  void addMessageParcel(const String& messageParcel);

  // Missing-IDs helpers (request/retry logic)
  String getFirstMissingParcel() const;
  std::vector<String> getMissingParcels() const;

  // Utility (sender side) – populate header + data parcels from full text
  // (normally called by the ctor when singleMessage==false).
  void splitDataIntoParcels();

private:
  // State
  bool   messageCompleted = false;
  String id;               // 2 letters (A–Z)
  String idFromSender;     // origin
  String idDestination;    // destination
  String message;          // full text when completed (or original for sender)
  String checksum;         // 4 letters A–Z
  std::map<String,String,StringLess> messageBox; // parcelId -> full parcel payload
  const uint64_t timeStamp; // local creation time (ms)

  // Helpers
  static uint64_t currentMillis64();
  static bool     isSingleCommand(const String& s);
  static void     splitByColon(const String& src, std::vector<String>& out, int maxParts);
  String          idPrefix() const;
  String          calculateChecksum(const String& data) const;
  static String   generateRandomId();
};
