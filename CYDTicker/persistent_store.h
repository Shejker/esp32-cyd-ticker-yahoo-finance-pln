#pragma once
#include <LittleFS.h>
#include <esp_partition.h>
#include <ArduinoJson.h>
#include <algorithm>

inline uint32_t crcBytes(uint32_t crc, const uint8_t* data, size_t size) {
  while (size--) {
    crc ^= *data++;
    for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return crc;
}

// A/B snapshots live in the existing filesystem partition, not the 20 KB NVS.
// A failed write never replaces the current valid snapshot or the legacy NVS.
class PersistentStore {
  static constexpr uint32_t MAGIC = 0x43594431;
  const char* slots[2] = {"/portfolio-a.dat", "/portfolio-b.dat"};
  int active = -1;
  uint32_t generation = 0;
  bool ready = false;
  bool recoveredBackup = false;
  bool validate(const char* path, uint32_t& gen) {
    File file = LittleFS.open(path, "r"); uint32_t header[4];
    if (!file || file.read((uint8_t*)header, sizeof(header)) != sizeof(header)
        || header[0] != MAGIC || !header[1] || header[1] > 65536
        || file.size() != sizeof(header) + header[1]) return false;
    uint32_t crc = ~0U; uint8_t bytes[256]; size_t left = header[1];
    while (left) {
      size_t n = file.read(bytes, std::min(left, sizeof(bytes)));
      if (!n) return false;
      crc = crcBytes(crc, bytes, n); left -= n;
    }
    gen = header[2]; return gen && ~crc == header[3];
  }
  bool blankPartition() {
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
      ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (!partition) return false;
    uint8_t bytes[256];
    for (size_t offset = 0; offset < partition->size; offset += sizeof(bytes)) {
      if (esp_partition_read(partition, offset, bytes, sizeof(bytes)) != ESP_OK) return false;
      for (uint8_t b : bytes) if (b != 0xff) return false;
    }
    return true;
  }
public:
  bool begin() {
    ready = LittleFS.begin(false);
    // Format only a verified completely blank partition. Never auto-format an
    // existing filesystem, and never erase the legacy preferences partition.
    if (!ready && blankPartition() && LittleFS.format()) ready = LittleFS.begin(false);
    return ready;
  }
  bool load(JsonDocument& doc) {
    if (!ready) return false;
    for (int i = 0; i < 2; ++i) {
      uint32_t gen;
      bool valid = validate(slots[i], gen);
      if (!valid && LittleFS.exists(slots[i])) recoveredBackup = true;
      if (valid && gen > generation) { generation = gen; active = i; }
    }
    if (active < 0) {
      if (LittleFS.exists(slots[0]) || LittleFS.exists(slots[1])) ready = false;
      return false;
    }
    File file = LittleFS.open(slots[active], "r"); file.seek(16);
    if (deserializeJson(doc, file)) { ready = false; return false; }
    return true;
  }
  bool save(const JsonDocument& doc) {
    if (!ready || generation == UINT32_MAX) return false;
    int next = active == 0 ? 1 : 0;
    // Temporary file + CRC verification + atomic rename on LittleFS.
    const char* temporary = "/portfolio-writing.dat";
    File file = LittleFS.open(temporary, "w");
    uint32_t header[4] = {MAGIC, (uint32_t)measureJson(doc), generation + 1, 0};
    if (!file || header[1] > 65536 || file.write((uint8_t*)header, sizeof(header)) != sizeof(header)
        || serializeJson(doc, file) != header[1]) return false;
    file.flush(); file.close();
    File check = LittleFS.open(temporary, "r");
    if (!check || check.size() != sizeof(header) + header[1] || !check.seek(sizeof(header))) return false;
    uint32_t crc = ~0U; uint8_t bytes[256]; size_t left = header[1];
    while (left) {
      size_t n = check.read(bytes, std::min(left, sizeof(bytes)));
      if (!n) return false;
      crc = crcBytes(crc, bytes, n); left -= n;
    }
    check.close(); header[3] = ~crc;
    file = LittleFS.open(temporary, "r+");
    if (!file || file.write((uint8_t*)header, sizeof(header)) != sizeof(header)) return false;
    file.flush(); file.close();
    uint32_t verified;
    if (!validate(temporary, verified) || verified != header[2]
        || !LittleFS.rename(temporary, slots[next])) return false;
    active = next; generation = header[2]; recoveredBackup = false; return true;
  }
  bool writable() const { return ready; }
  uint32_t revision() const { return generation; }
  bool recovered() const { return recoveredBackup; }
  void disable() { ready = false; }
};
