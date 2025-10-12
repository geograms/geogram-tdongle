// messages.cpp — segmented append-only message log with in-file dedupe and size-based parsing
// Record format (ASCII, single record = one logical "line"):
//   message_line_size|checksum|timestamp|message_type|message_content
//
// - message_line_size: decimal, total bytes of the entire record INCLUDING everything after it (no trailing '\n' required)
// - checksum         : fixed-length hex string (tunable length)
// - timestamp        : fixed-length "YYYY-MM-DD_HH:MM_SS" (19 chars)
// - message_type     : exactly 3 chars
// - message_content  : 0..MSG_MAX_CONTENT_BYTES, may contain ANY chars (including '\n' and '|')
//
// The library reads records by the declared size prefix (not by newline).
// Dedupe: per-segment, keyed by checksum. Query order: strict append order (segment seq DESC, then record order DESC).

#ifdef MSG_HOST_TEST
  #include "host_arduino.h"
  #include "host_fs.h"
#else
  #include <Arduino.h>
  #include <FS.h>
#endif


#include <algorithm>
#include <unordered_set>

#include "messages.h"
#include <algorithm>
#include <unordered_set>

// ---------- Tunables (change here then rebuild) ----------
#ifndef MSG_SEGMENT_BYTES
#define MSG_SEGMENT_BYTES          (1024 * 1024)   // 1 MB per segment
#endif
#ifndef MSG_DIR_PATH
#define MSG_DIR_PATH               "/messages"
#endif
#ifndef MSG_FILE_PREFIX
#define MSG_FILE_PREFIX            "messages"
#endif
#ifndef MSG_FILE_EXT
#define MSG_FILE_EXT               ".txt"
#endif
#ifndef MSG_CHECKSUM_HEX_LEN
#define MSG_CHECKSUM_HEX_LEN       8               // e.g., CRC32 => 8 hex chars
#endif
#ifndef MSG_TIMESTAMP_LEN
#define MSG_TIMESTAMP_LEN          19              // "YYYY-MM-DD_HH:MM_SS"
#endif
#ifndef MSG_TYPE_LEN
#define MSG_TYPE_LEN               3
#endif
#ifndef MSG_MAX_CONTENT_BYTES
#define MSG_MAX_CONTENT_BYTES      10000
#endif
#ifndef MSG_FLUSH_EVERY_N
#define MSG_FLUSH_EVERY_N          50              // flush every N appends
#endif
#ifndef MSG_SEQ_FILE
#define MSG_SEQ_FILE               ".seq"          // stores last used sequence (decimal)
#endif

// ---------- Internals ----------
static fs::FS* g_fs = nullptr;
static String  g_dir;
static size_t  g_curSeq = 0;
static File    g_curFile;
static size_t  g_curBytes = 0;
static size_t  g_sinceFlush = 0;

// In-segment dedupe by checksum (rebuilt when opening the tail)
static std::unordered_set<std::string> g_seenChecksums;

// ---- Helpers ----
static String seqToName(size_t seq) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%08u", (unsigned)seq);
  String path = g_dir; path += "/"; path += MSG_FILE_PREFIX; path += buf; path += MSG_FILE_EXT;
  return path;
}

static String seqFilePath() {
  String p = g_dir; p += "/"; p += MSG_SEQ_FILE; return p;
}

static bool ensureDir() {
  if (!g_fs->exists(g_dir)) {
    if (!g_fs->mkdir(g_dir)) return false;
  }
  return true;
}

static bool readLastSeqFromFile(size_t& out) {
  out = 0;
  String p = seqFilePath();
  if (!g_fs->exists(p)) return true; // not an error
  File f = g_fs->open(p, FILE_READ);
  if (!f) return false;
  String s = f.readString(); f.close();
  s.trim();
  if (s.length() == 0) return true;
  out = (size_t) s.toInt();
  return true;
}

static bool writeLastSeqToFile(size_t v) {
  String p = seqFilePath();
  File f = g_fs->open(p, FILE_WRITE);
  if (!f) return false;
  f.print(String((unsigned)v));
  f.close();
  return true;
}

static bool listExistingSeqs(std::vector<size_t>& outSeqs) {
  outSeqs.clear();
  File dir = g_fs->open(g_dir);
  if (!dir) return false;
  for (;;) {
    File f = dir.openNextFile();
    if (!f) break;
    String name = f.name();  // full path
    f.close();
    int slash = name.lastIndexOf('/');
    String base = slash >= 0 ? name.substring(slash + 1) : name;
    if (!base.startsWith(MSG_FILE_PREFIX) || !base.endsWith(MSG_FILE_EXT)) continue;
    String mid = base.substring(strlen(MSG_FILE_PREFIX), base.length() - strlen(MSG_FILE_EXT));
    if (mid.length() != 8) continue;
    // do not treat ".seq" or other files as segments
    size_t seq = (size_t) mid.toInt();
    if (seq > 0) outSeqs.push_back(seq);
  }
  return true;
}

// Read next full record using the declared size prefix.
static bool readNextRecordBySize(File& f, String& outLine) {
  // 1) read size digits until first '|'
  String sizeStr;
  for (;;) {
    int c = f.read();
    if (c < 0) return false;          // EOF
    if (c == '|') break;
    if (c < '0' || c > '9') return false; // malformed
    sizeStr += char(c);
    if (sizeStr.length() > 10) return false; // sanity cap
  }
  if (sizeStr.length() == 0) return false;

  // 2) parse declared size and then read remaining bytes
  size_t declared = (size_t) sizeStr.toInt();
  size_t prefixLen = sizeStr.length() + 1; // digits + '|'
  if (declared < prefixLen) return false;
  size_t remaining = declared - prefixLen;

  outLine.reserve(declared);
  outLine = sizeStr; outLine += '|';

  char buf[256];
  while (remaining > 0) {
    size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
    int got = f.readBytes(buf, want);
    if (got <= 0) return false;
    outLine.concat(buf, got);
    remaining -= (size_t)got;
  }
  return true;
}

static bool openNewSegment() {
  if (g_curFile) g_curFile.close();
  g_seenChecksums.clear();

  g_curSeq += 1;
  if (!writeLastSeqToFile(g_curSeq)) return false;

  String path = seqToName(g_curSeq);
  g_curFile = g_fs->open(path, FILE_WRITE);
  if (!g_curFile) return false;
  g_curBytes = g_curFile.size();
  g_sinceFlush = 0;
  return true;
}

static bool rebuildDedupeForTail() {
  g_seenChecksums.clear();
  if (g_curSeq == 0) return true;
  File f = g_fs->open(seqToName(g_curSeq), FILE_READ);
  if (!f) return false;
  String line;
  while (readNextRecordBySize(f, line)) {
    int p1 = line.indexOf('|'); if (p1 < 0) continue;
    int p2 = line.indexOf('|', p1 + 1); if (p2 < 0) continue;
    String checksum = line.substring(p1 + 1, p2);
    if ((int)checksum.length() == MSG_CHECKSUM_HEX_LEN) {
      g_seenChecksums.insert(std::string(checksum.c_str()));
    }
  }
  f.close();
  return true;
}

static bool openOrCreateTail() {
  // derive current sequence: max(existingSegments), then max(.seq, existingMax)
  size_t maxSeqFiles = 0, seqFileVal = 0;
  std::vector<size_t> seqs;
  listExistingSeqs(seqs);
  for (auto s : seqs) if (s > maxSeqFiles) maxSeqFiles = s;
  if (!readLastSeqFromFile(seqFileVal)) return false;

  g_curSeq = std::max(maxSeqFiles, seqFileVal);

  // open tail if exists and under size; else new segment
  if (g_curSeq > 0) {
    String path = seqToName(g_curSeq);
    g_curFile = g_fs->open(path, FILE_APPEND);
    if (!g_curFile) return false;
    g_curBytes = g_curFile.size();
  }
  if (!g_curFile || g_curBytes >= MSG_SEGMENT_BYTES) {
    if (!openNewSegment()) return false;
  }
  g_sinceFlush = 0;
  return rebuildDedupeForTail();
}

static bool rotateIfNeeded(size_t nextLineBytes) {
  if (g_curBytes + nextLineBytes <= MSG_SEGMENT_BYTES) return true;
  return openNewSegment();
}

static bool parseLine(const String& line, MessageView& out) {
  // Expect: size|checksum|timestamp|type|content...
  int p1 = line.indexOf('|'); if (p1 < 0) return false;
  int p2 = line.indexOf('|', p1 + 1); if (p2 < 0) return false;
  int p3 = line.indexOf('|', p2 + 1); if (p3 < 0) return false;
  int p4 = line.indexOf('|', p3 + 1); if (p4 < 0) return false;

  out.line = line;
  out.lineSize = (size_t) line.substring(0, p1).toInt();
  out.checksum = line.substring(p1 + 1, p2);
  out.timestamp = line.substring(p2 + 1, p3);
  out.type3 = line.substring(p3 + 1, p4);
  out.content = line.substring(p4 + 1); // can include any chars (including '\n' and '|')

  if ((int)out.checksum.length()   != MSG_CHECKSUM_HEX_LEN) return false;
  if ((int)out.timestamp.length()  != MSG_TIMESTAMP_LEN)    return false;
  if ((int)out.type3.length()      != MSG_TYPE_LEN)         return false;
  // content length limit is enforced on write; skip here
  return true;
}

static bool passesFilter(const MessageView& mv, const MsgFilter& f) {
  if (f.type3 && *f.type3) {
    if (mv.type3.length() != 3 || !mv.type3.equalsIgnoreCase(f.type3)) return false;
  }
  if (f.contentSubstr && *f.contentSubstr) {
    if (mv.content.indexOf(f.contentSubstr) < 0) return false;
  }
  if (f.tsFrom && *f.tsFrom) {
    if (mv.timestamp < f.tsFrom) return false; // string compare works for fixed format
  }
  if (f.tsTo && *f.tsTo) {
    if (mv.timestamp > f.tsTo) return false;
  }
  return true;
}

// ---------- Public API ----------
bool msg_init(fs::FS& fs, const char* directory) {
  g_fs = &fs;
  g_dir = (directory && *directory) ? directory : String(MSG_DIR_PATH);
  if (!ensureDir()) return false;
  return openOrCreateTail();
}

void msg_end() {
  if (g_curFile) g_curFile.close();
  g_fs = nullptr;
  g_seenChecksums.clear();
}

bool msg_roll_segment() {
  return openNewSegment();
}

size_t msg_current_seq()   { return g_curSeq; }
size_t msg_current_bytes() { return g_curBytes; }

bool msg_write(const String& checksum,
               const String& timestamp,
               const String& type3,
               const String& content)
{
  if (!g_fs || !g_curFile) return false;

  // Validate fixed fields
  if ((int)checksum.length() != MSG_CHECKSUM_HEX_LEN) return false;
  if ((int)timestamp.length() != MSG_TIMESTAMP_LEN)  return false;
  if ((int)type3.length()   != MSG_TYPE_LEN)         return false;
  if ((int)content.length() > MSG_MAX_CONTENT_BYTES) return false;

  // Per-segment dedupe by checksum
  if (g_seenChecksums.find(std::string(checksum.c_str())) != g_seenChecksums.end()) {
    return false; // duplicate within current file
  }

  // Build body and compute final line size (self-consistent digit count)
  String body = checksum + "|" + timestamp + "|" + type3 + "|" + content;
  size_t sizeDigits = 1;
  while (true) {
    size_t sz = sizeDigits + 1 /*'|'*/ + body.length();
    String sDigits = String(sz);
    if ((size_t)sDigits.length() == sizeDigits) {
      String line; line.reserve(sz);
      line += sDigits; line += "|"; line += body;

      if (!rotateIfNeeded(line.length())) return false;

      size_t written = g_curFile.print(line);
      if (written != line.length()) return false;

      g_curBytes += written;
      g_sinceFlush++;
      g_seenChecksums.insert(std::string(checksum.c_str()));

      if (g_sinceFlush >= MSG_FLUSH_EVERY_N) {
        g_curFile.flush();
        g_sinceFlush = 0;
      }
      return true;
    }
    sizeDigits = sDigits.length();
  }
}

bool msg_query(const MsgFilter& filter, size_t limit, std::vector<MessageView>& out) {
  out.clear();
  if (!g_fs) return false;

  std::vector<size_t> seqs;
  if (!listExistingSeqs(seqs)) return false;
  if (seqs.empty()) return true;

  std::sort(seqs.begin(), seqs.end()); // ascending
  for (int i = (int)seqs.size() - 1; i >= 0 && out.size() < limit; --i) {
    File f = g_fs->open(seqToName(seqs[i]), FILE_READ);
    if (!f) continue;

    std::vector<MessageView> bucket;
    bucket.reserve(256);

    String line;
    while (readNextRecordBySize(f, line)) {
      MessageView mv;
      if (!parseLine(line, mv)) continue;
      if (passesFilter(mv, filter)) {
        bucket.push_back(std::move(mv));
      }
    }
    f.close();

    // Append newest-first within this file (reverse of append order)
    for (int j = (int)bucket.size() - 1; j >= 0 && out.size() < limit; --j) {
      out.push_back(std::move(bucket[j]));
    }
  }
  return true;
}

size_t msg_count_total() {
  if (!g_fs) return 0;
  std::vector<size_t> seqs;
  if (!listExistingSeqs(seqs)) return 0;

  size_t total = 0;
  for (size_t s : seqs) {
    File f = g_fs->open(seqToName(s), FILE_READ);
    if (!f) continue;
    String line;
    while (readNextRecordBySize(f, line)) {
      // minimal structure validation to avoid counting corrupt tails
      int p1 = line.indexOf('|'); if (p1 < 0) continue;
      int p2 = line.indexOf('|', p1 + 1); if (p2 < 0) continue;
      int p3 = line.indexOf('|', p2 + 1); if (p3 < 0) continue;
      int p4 = line.indexOf('|', p3 + 1); if (p4 < 0) continue;
      // sanity on lengths
      if ((p2 - (p1 + 1)) != MSG_CHECKSUM_HEX_LEN) continue;
      if ((p3 - (p2 + 1)) != MSG_TIMESTAMP_LEN) continue;
      if ((p4 - (p3 + 1)) != MSG_TYPE_LEN) continue;
      total++;
    }
    f.close();
  }
  return total;
}

bool msg_delete_all(bool resetSequence) {
  if (!g_fs) return false;

  // Close current file first
  if (g_curFile) { g_curFile.close(); g_curFile = File(); }
  g_seenChecksums.clear();
  g_curBytes = 0;

  // Delete all segment files
  File dir = g_fs->open(g_dir);
  if (!dir) return false;
  for (;;) {
    File f = dir.openNextFile();
    if (!f) break;
    String name = f.name();
    f.close();
    int slash = name.lastIndexOf('/');
    String base = slash >= 0 ? name.substring(slash + 1) : name;

    if (base == MSG_SEQ_FILE) continue; // treat separately below
    if (base.startsWith(MSG_FILE_PREFIX) && base.endsWith(MSG_FILE_EXT)) {
      g_fs->remove(name);
    }
  }

  // Handle sequence file
  if (resetSequence) {
    g_fs->remove(seqFilePath());
    g_curSeq = 0;
  } else {
    // keep .seq so numbering continues
    size_t lastSeq = 0;
    readLastSeqFromFile(lastSeq);
    g_curSeq = lastSeq;
  }

  // Open a fresh segment
  return openNewSegment();
}
