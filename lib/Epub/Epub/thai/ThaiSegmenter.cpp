#include "ThaiSegmenter.h"

#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "ThaiLexicon.h"
#include "generated/thaiLexicon.generated.h"

namespace {

// Maximum number of clusters to attempt in forward maximal matching. Lexicon
// words span at most 15 clusters, apart from one 28-cluster title that is never
// matched.
constexpr size_t MAX_WORD_CLUSTERS = 20;

// Longest byte sequence worth looking up — derived from ThaiLexicon.h's
// constant so the two can never drift apart.
constexpr size_t MAX_LOOKUP_BYTES = THAI_LEXICON_MAX_ENTRY_BYTES;

const ThaiLexicon& getThaiLexicon() { return thai_lexicon; }

inline bool isThaiConsonant(uint32_t cp) { return cp >= 0x0E01 && cp <= 0x0E2E; }

inline bool isThaiDigit(uint32_t cp) { return cp >= 0x0E50 && cp <= 0x0E59; }

// Spacing vowels written after their consonant (Sara A, Sara Aa, Lakkhangyao).
// They belong to its syllable and no word starts with one, so the cluster keeps
// them; lexicon words still end on a cluster boundary.
inline bool isThaiFollowingVowel(uint32_t cp) { return cp == 0x0E30 || cp == 0x0E32 || cp == 0x0E45; }

struct ThaiCluster {
  size_t startOffset;  // byte offset of base char
  size_t endOffset;    // byte offset just past last mark (or base if no marks)
};

// Parse a Thai run into clusters: each cluster is a base char + its following
// combining marks (vowels, tone marks) and spacing vowels. A leading vowel is
// grouped with the following consonant cluster, a run of Thai digits forms one
// cluster, and an orphan combining mark may start a cluster when the source is
// malformed.
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
    // Thai leading vowels are encoded before their consonant. Keep the pair
    // atomic so OOV fallback cannot split the orthographic syllable.
    if (utf8IsThaiLeadingVowel(cp) && ptr < endPtr) {
      const auto* beforeConsonant = ptr;
      const uint32_t consonant = utf8NextCodepoint(&ptr);
      if (consonant == 0 || !isThaiConsonant(consonant)) {
        ptr = beforeConsonant;
      }
    }
    // Consume following combining marks and spacing vowels. Sara Am arrives
    // decomposed as Nikhahit + Sara Aa, possibly with a tone between them.
    const bool digits = isThaiDigit(cp);
    while (ptr < endPtr) {
      const auto* beforeCp = ptr;
      const uint32_t nextCp = utf8NextCodepoint(&ptr);
      if (utf8IsCombiningMark(nextCp) || (digits ? isThaiDigit(nextCp) : isThaiFollowingVowel(nextCp))) {
        continue;
      }
      ptr = beforeCp;
      break;
    }
    clusters.push_back({clusterStart, static_cast<size_t>(ptr - base)});
  }
  return clusters;
}

// No line may start with a sign that closes a syllable or word (Mai Yamok and
// Paiyannoi are clusters of their own) or inside the abbreviation ฯลฯ.
bool thaiBreakAllowedAt(const std::string& text, const size_t offset) {
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str()) + offset;
  const uint32_t next = utf8NextCodepoint(&p);
  if (utf8IsThaiNonStarter(next)) return false;
  // ฯ|ลฯ: the break follows a Paiyannoi (3 bytes) and Lo Ling + Paiyannoi follow it.
  return !(next == 0x0E25 && offset >= 3 && text.compare(offset - 3, 3, "\xE0\xB8\xAF") == 0 &&
           utf8NextCodepoint(&p) == 0x0E2F);
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
    if (utf8IsThaiCodepoint(cp)) return true;
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

    if (utf8IsThaiCodepoint(cp)) {
      hasThai = true;
      // Find end of the Thai run (maximal sequence of Thai codepoints).
      while (*ptr) {
        const auto* beforeCp = ptr;
        uint32_t nextCp = utf8NextCodepoint(&ptr);
        if (nextCp == 0 || !utf8IsThaiCodepoint(nextCp)) {
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
  breaks.erase(std::remove_if(breaks.begin(), breaks.end(),
                              [&](const size_t offset) { return !thaiBreakAllowedAt(text, offset); }),
               breaks.end());

  return breaks;
}
