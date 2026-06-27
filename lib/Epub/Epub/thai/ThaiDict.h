#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Descriptor pointing at a flash-resident Thai word dictionary.
// Binary format (little-endian):
//   [4 bytes: uint32 entry_count]
//   [4 * entry_count bytes: uint32 offsets from blob start, one per entry]
//   [string pool: null-terminated UTF-8 strings, sorted by memcmp]
// Offsets are absolute from the start of the data blob. The offset table and
// string pool are laid out so that binary search over offsets yields entries
// in UTF-8 byte order (memcmp order), matching strcmp semantics.
struct ThaiDict {
  const uint8_t* data;
  size_t size;
  uint32_t entryCount;
  uint32_t stringPoolOffset;
};

// Binary-search the flash-resident dictionary for an exact match.
// str/len is a UTF-8 byte sequence (not necessarily null-terminated).
// Returns true if the sequence matches a dictionary entry.
// Uses a 128-byte stack buffer for null-termination; sequences >= 128 bytes
// return false (no match). This is sufficient for all realistic Thai words
// (max ~42 Thai chars, each 3 UTF-8 bytes).
inline bool thaiDictContains(const ThaiDict& dict, const char* str, size_t len) {
  if (len == 0 || len >= 128 || dict.entryCount == 0) return false;
  char buf[128];
  memcpy(buf, str, len);
  buf[len] = '\0';
  int lo = 0;
  int hi = static_cast<int>(dict.entryCount) - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    // memcpy for the offset read — RISC-V faults on unaligned multi-byte loads.
    // The data is alignas(4) and offsets are at 4+4*mid (always aligned), but
    // follow the project rule: never cast uint8_t* to a wider pointer.
    uint32_t offset;
    memcpy(&offset, dict.data + 4 + static_cast<size_t>(mid) * 4, sizeof(offset));
    const char* entry = reinterpret_cast<const char*>(dict.data + offset);
    const int cmp = strcmp(buf, entry);
    if (cmp == 0) return true;
    if (cmp < 0) {
      hi = mid - 1;
    } else {
      lo = mid + 1;
    }
  }
  return false;
}
