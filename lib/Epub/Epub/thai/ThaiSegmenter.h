#pragma once

#include <array>
#include <cstddef>
#include <string>

// Returns true if `text` contains any Thai codepoint (U+0E01–U+0E5B).
// Used to gate the Thai segmentation path before the CJK fallback.
bool containsThaiCodepoint(const std::string& text);

// Reusable storage for bounded Thai segmentation. ChapterHtmlSlimParser limits
// source words to 200 bytes, so one contains at most 66 Thai codepoints. Keep
// this heap-owned with ParsedText instead of allocating once per source word.
struct ThaiSegmentationScratch {
  struct Cluster {
    size_t startOffset;
    size_t endOffset;
  };

  static constexpr size_t MAX_CLUSTERS = 67;
  static constexpr size_t MAX_BREAK_OFFSETS = 200;
  std::array<Cluster, MAX_CLUSTERS> clusters;
  std::array<size_t, MAX_BREAK_OFFSETS> breakOffsets;
  size_t clusterCount = 0;
  size_t breakCount = 0;
};

// Writes byte offsets within `text` where line breaks are allowed for Thai
// script. Uses forward maximal matching against a flash-resident dictionary
// and adds breaks at Thai<->non-Thai transitions. Returns false if `text`
// exceeds the fixed scratch capacity; the caller must fail the affected build
// rather than silently losing legal line breaks.
bool thaiWordBreakByteOffsets(const std::string& text, ThaiSegmentationScratch& scratch);
