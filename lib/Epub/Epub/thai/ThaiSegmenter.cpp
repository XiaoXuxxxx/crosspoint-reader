#include "ThaiSegmenter.h"

#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "ThaiLexicon.h"
#include "generated/thaiLexicon.generated.h"

namespace {

// Maximum number of clusters to attempt in forward maximal matching.
// Thai words rarely exceed 20 clusters (base+marks); longer attempts would
// exceed the lookup buffer and are skipped by thaiLexiconContains anyway.
constexpr size_t MAX_WORD_CLUSTERS = 20;

// Stack buffer size for lexicon lookup in thaiLexiconContains() — derived from
// ThaiLexicon.h's constant so the two can never drift apart.
constexpr size_t MAX_LOOKUP_BYTES = THAI_LEXICON_MAX_ENTRY_BYTES;

const ThaiLexicon& getThaiLexicon() { return thai_lexicon; }

inline bool isThaiCodepoint(uint32_t cp) { return cp >= 0x0E01 && cp <= 0x0E5B; }

struct ThaiCluster {
  size_t startOffset;  // byte offset of base char
  size_t endOffset;    // byte offset just past last mark (or base if no marks)
};

// Parse a Thai run into clusters: each cluster is a base char + its following
// combining marks (vowels, tone marks). Combining marks never start a cluster.
std::vector<ThaiCluster> parseClusters(const std::string& text, size_t start, size_t end) {
  std::vector<ThaiCluster> clusters;
  clusters.reserve((end - start) / 3);  // Thai codepoints are 3 UTF-8 bytes; a cluster is >= 1 codepoint.
  const auto* base = reinterpret_cast<const unsigned char*>(text.c_str());
  const auto* ptr = base + start;
  const auto* const endPtr = base + end;

  while (ptr < endPtr) {
    const size_t clusterStart = static_cast<size_t>(ptr - base);
    uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) break;
    // Consume following combining marks (Thai vowels, tone marks, Nikhahit).
    while (ptr < endPtr) {
      const auto* beforeCp = ptr;
      uint32_t nextCp = utf8NextCodepoint(&ptr);
      if (nextCp == 0 || !utf8IsCombiningMark(nextCp)) {
        ptr = beforeCp;
        break;
      }
    }
    clusters.push_back({clusterStart, static_cast<size_t>(ptr - base)});
  }
  return clusters;
}

// Segment a Thai run using forward maximal matching against the lexicon.
// Adds word-boundary byte offsets to `breaks`. Unknown sequences fall back to
// single-cluster tokens (equivalent to the old char-level break behavior).
void segmentThaiRun(const std::string& text, size_t start, size_t end, const ThaiLexicon& lexicon,
                    std::vector<size_t>& breaks) {
  auto clusters = parseClusters(text, start, end);
  if (clusters.size() < 2) return;

  size_t pos = 0;
  while (pos < clusters.size()) {
    // Default: single cluster (no lexicon match). This means unknown words
    // break at every cluster, matching the previous char-level behavior.
    size_t bestEnd = pos;
    const size_t maxLen = std::min(MAX_WORD_CLUSTERS, clusters.size() - pos);
    for (size_t len = maxLen; len >= 2; --len) {
      const size_t substrStart = clusters[pos].startOffset;
      const size_t substrEnd = clusters[pos + len - 1].endOffset;
      const size_t substrLen = substrEnd - substrStart;
      if (substrLen >= MAX_LOOKUP_BYTES) continue;
      if (thaiLexiconContains(lexicon, text.c_str() + substrStart, substrLen)) {
        bestEnd = pos + len - 1;
        break;
      }
    }
    // Add break at end of this word (if not at the end of the run).
    if (bestEnd + 1 < clusters.size()) {
      breaks.push_back(clusters[bestEnd].endOffset);
    }
    pos = bestEnd + 1;
  }
}

}  // namespace

bool containsThaiCodepoint(const std::string& text) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*ptr) {
    uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) break;
    if (isThaiCodepoint(cp)) return true;
  }
  return false;
}

std::vector<size_t> thaiWordBreakByteOffsets(const std::string& text) {
  std::vector<size_t> breaks;
  breaks.reserve(text.size() / 3 + 1);  // Generous upper bound: breaks are far rarer than codepoints.

  const auto* base = reinterpret_cast<const unsigned char*>(text.c_str());
  const auto* ptr = base;
  bool hasThai = false;

  while (*ptr) {
    const auto* runStart = ptr;
    uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) break;

    if (isThaiCodepoint(cp)) {
      hasThai = true;
      // Find end of the Thai run (maximal sequence of Thai codepoints).
      while (*ptr) {
        const auto* beforeCp = ptr;
        uint32_t nextCp = utf8NextCodepoint(&ptr);
        if (nextCp == 0 || !isThaiCodepoint(nextCp)) {
          ptr = beforeCp;
          break;
        }
      }
      const size_t runStartOffset = static_cast<size_t>(runStart - base);
      const size_t runEndOffset = static_cast<size_t>(ptr - base);

      // Break before the Thai run (Thai -> non-Thai boundary).
      if (runStartOffset > 0) {
        breaks.push_back(runStartOffset);
      }

      // Segment the Thai run into words.
      segmentThaiRun(text, runStartOffset, runEndOffset, getThaiLexicon(), breaks);

      // Break after the Thai run (non-Thai -> Thai boundary).
      if (runEndOffset < text.size()) {
        breaks.push_back(runEndOffset);
      }
    }
  }

  if (!hasThai) return {};

  // Sort and dedup breaks (Thai/non-Thai boundaries may coincide with word
  // boundaries from segmentThaiRun).
  std::sort(breaks.begin(), breaks.end());
  breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

  return breaks;
}
