#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Descriptor pointing at a flash-resident, front-coded Thai word lexicon.
// Binary format (little-endian), produced by gen_thai_lexicon.py:
//   [4 bytes: uint32 restart_count]
//   [4 * restart_count bytes: uint32 absolute offset of each block's first
//      record, from the blob start]
//   [blocks ...]
// Entries are sorted by UTF-8 byte order (memcmp order) and grouped into blocks
// of up to RESTART_INTERVAL (16) entries. Within a block:
//   - first record (the restart point): [u8 key_len][key_len bytes]
//   - each later record:                [u8 shared_len][u8 suffix_len][suffix]
//     full word = previous_word[:shared_len] + suffix.
// A block runs until the next block's offset (the last block ends at `size`).
// The sparse restart table is binary-searched to find the candidate block; the
// block is then scanned linearly, reconstructing each entry. Sorted order lets
// the scan stop early once it passes the target.
struct ThaiLexicon {
  const uint8_t* data;
  size_t size;
  uint32_t restartCount;
};

// Maximum reconstructed entry length in bytes. The generator (gen_thai_lexicon.py)
// drops any entry >= this many UTF-8 bytes; real Thai words are far shorter (~42
// chars max, 3 bytes each), so this only guards corrupt input. ThaiSegmenter.cpp's
// MAX_LOOKUP_BYTES is defined in terms of this constant; the Python-side copy
// (MAX_WORD_BYTES) can't share it directly (cross-language) and is kept in sync by hand.
constexpr size_t THAI_LEXICON_MAX_ENTRY_BYTES = 128;

namespace thailexicon_detail {

// Lexicographic compare of two byte sequences (UTF-8 byte order == unsigned
// byte order == strcmp semantics; shorter sequence sorts before its extensions).
inline int compareBytes(const char* a, size_t aLen, const char* b, size_t bLen) {
  const size_t n = aLen < bLen ? aLen : bLen;
  const int c = std::memcmp(a, b, n);
  if (c != 0) return c;
  if (aLen < bLen) return -1;
  if (aLen > bLen) return 1;
  return 0;
}

// Read a little-endian uint32 from the blob. memcpy because RISC-V faults on
// unaligned multi-byte loads and the project forbids casting uint8_t* wider.
inline uint32_t readU32(const uint8_t* data, size_t off) {
  uint32_t v;
  std::memcpy(&v, data + off, sizeof(v));
  return v;
}

}  // namespace thailexicon_detail

// Look up the flash-resident lexicon for an exact match.
// str/len is a UTF-8 byte sequence (not necessarily null-terminated).
// Returns true if the sequence matches a lexicon entry. Sequences of
// THAI_LEXICON_MAX_ENTRY_BYTES or more return false (no match) — see that
// constant's comment for why this bound is safe.
inline bool thaiLexiconContains(const ThaiLexicon& lexicon, const char* str, size_t len) {
  if (len == 0 || len >= THAI_LEXICON_MAX_ENTRY_BYTES || lexicon.restartCount == 0) return false;
  using namespace thailexicon_detail;

  const uint8_t* data = lexicon.data;

  // Helper: compare the target against the restart key at block index `i`.
  auto cmpRestartKey = [&](uint32_t i) -> int {
    const uint32_t off = readU32(data, 4 + static_cast<size_t>(i) * 4);
    const uint8_t keyLen = data[off];
    const char* key = reinterpret_cast<const char*>(data + off + 1);
    return compareBytes(str, len, key, keyLen);
  };

  // If the target precedes the first entry, it cannot be present.
  if (cmpRestartKey(0) < 0) return false;

  // Binary search for the last restart key that is <= the target. The matching
  // entry, if any, lives in that block (block[i].first <= target < block[i+1].first).
  uint32_t lo = 0;
  uint32_t hi = lexicon.restartCount - 1;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo + 1) / 2;  // upper mid
    // cmpRestartKey(mid) >= 0 means key(mid) <= target, so the match (if any)
    // is in this block or a later one — move the lower bound up.
    if (cmpRestartKey(mid) >= 0) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  const uint32_t block = lo;

  // Scan the block, reconstructing each entry from its predecessor.
  size_t p = readU32(data, 4 + static_cast<size_t>(block) * 4);
  const size_t end =
      (block + 1 < lexicon.restartCount) ? readU32(data, 4 + static_cast<size_t>(block + 1) * 4) : lexicon.size;

  char cur[THAI_LEXICON_MAX_ENTRY_BYTES];
  size_t curLen = 0;
  bool first = true;
  while (p < end) {
    size_t shared;
    size_t suffix;
    if (first) {
      shared = 0;
      suffix = data[p++];
      first = false;
    } else {
      shared = data[p++];
      suffix = data[p++];
    }
    // cur[0..shared) already holds the shared prefix from the previous entry.
    std::memcpy(cur + shared, data + p, suffix);
    p += suffix;
    curLen = shared + suffix;

    const int cmp = compareBytes(str, len, cur, curLen);
    if (cmp == 0) return true;
    if (cmp < 0) return false;  // passed the target; entries are sorted ascending
  }
  return false;
}
