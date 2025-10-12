#pragma once
#ifdef MSG_HOST_TEST
  #include "host_arduino.h"
  #include "host_fs.h"
#else
  #include <Arduino.h>
  #include <FS.h>
#endif
#include <vector>

struct MsgFilter {
  const char* type3 = nullptr;         // 3-letter type (optional)
  const char* contentSubstr = nullptr; // substring in content (optional)
  const char* tsFrom = nullptr;        // inclusive "YYYY-MM-DD_HH:MM_SS" (optional)
  const char* tsTo   = nullptr;        // inclusive (optional)
};

struct MessageView {
  String line;        // full stored line (size|checksum|timestamp|type|content...)
  String checksum;    // fixed-length hex
  String timestamp;   // "YYYY-MM-DD_HH:MM_SS"
  String type3;       // 3 characters
  String content;     // can contain any chars (including '\n' and '|')
  size_t lineSize = 0;
};

bool   msg_init(fs::FS& fs, const char* directory); // mounts folder & opens tail (or creates)
void   msg_end();

// Write one message; deduped by checksum within current segment.
// Returns true on append, false if duplicate/invalid/error.
bool   msg_write(const String& checksum,
                 const String& timestamp,   // fixed 19 chars
                 const String& type3,       // exactly 3 chars
                 const String& content);    // up to MSG_MAX_CONTENT_BYTES, may include '\n' and '|'

// Query most-recent-first (append order): newest segments first, then newest records inside each segment.
bool   msg_query(const MsgFilter& filter, size_t limit, std::vector<MessageView>& out);

// Rotate to a new segment (sequence always increments).
bool   msg_roll_segment();

// Maintenance / stats
size_t msg_current_seq();     // current segment sequence number
size_t msg_current_bytes();   // current segment size in bytes
size_t msg_count_total();     // total messages across all segments

// Delete ALL segment files in the directory. If resetSequence==true, also reset numbering to start over.
bool   msg_delete_all(bool resetSequence);

// Optional: change in-memory dedupe scope/behavior later without format changes.
