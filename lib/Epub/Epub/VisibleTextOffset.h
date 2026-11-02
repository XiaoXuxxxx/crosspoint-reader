#pragma once

#include <Utf8.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace VisibleTextOffset {

namespace detail {

struct MappedCodepoint {
  uint32_t codepoint;
  uint32_t sourceCodepointCount;
};

// Streams the same NFC transformation as utf8ComposeNfc(), while retaining the
// number of source codepoints consumed by each emitted codepoint.
class NfcCodepointStream {
 public:
  explicit NfcCodepointStream(const std::string& source)
      : sourcePtr_(reinterpret_cast<const unsigned char*>(source.data())), sourceEnd_(sourcePtr_ + source.size()) {}

  bool next(MappedCodepoint& result) {
    if (pendingOutput_) {
      result = pendingOutputCodepoint_;
      pendingOutput_ = false;
      return true;
    }

    while (sourcePtr_ < sourceEnd_) {
      const uint32_t codepoint = utf8NextCodepoint(&sourcePtr_);
      ++sourceCodepointCount_;

      const uint32_t composed = hasPendingBase_ ? utf8ComposePair(pendingBase_, codepoint) : 0;
      if (composed != 0) {
        pendingBase_ = composed;
        pendingBaseSourceCount_ = sourceCodepointCount_;
        continue;
      }

      if (utf8IsCombiningMark(codepoint)) {
        if (!hasPendingBase_) {
          result = {codepoint, sourceCodepointCount_};
          return true;
        }

        result = {pendingBase_, pendingBaseSourceCount_};
        hasPendingBase_ = false;
        pendingOutputCodepoint_ = {codepoint, sourceCodepointCount_};
        pendingOutput_ = true;
        return true;
      }

      if (hasPendingBase_) {
        result = {pendingBase_, pendingBaseSourceCount_};
        pendingBase_ = codepoint;
        pendingBaseSourceCount_ = sourceCodepointCount_;
        return true;
      }

      pendingBase_ = codepoint;
      pendingBaseSourceCount_ = sourceCodepointCount_;
      hasPendingBase_ = true;
    }

    if (hasPendingBase_) {
      result = {pendingBase_, pendingBaseSourceCount_};
      hasPendingBase_ = false;
      return true;
    }
    return false;
  }

  uint32_t sourceCodepointCount() const { return sourceCodepointCount_; }

 private:
  const unsigned char* sourcePtr_;
  const unsigned char* sourceEnd_;
  uint32_t sourceCodepointCount_ = 0;
  uint32_t pendingBase_ = 0;
  uint32_t pendingBaseSourceCount_ = 0;
  MappedCodepoint pendingOutputCodepoint_ = {};
  bool hasPendingBase_ = false;
  bool pendingOutput_ = false;
};

inline bool isThaiToneMark(const uint32_t codepoint) { return codepoint >= 0x0E48 && codepoint <= 0x0E4B; }

// Streams the Sara Am expansion after NFC. Tone marks immediately before Sara
// Am are replayed from a copied stream so no unbounded temporary buffer is
// needed for the reordering step.
class RenderedCodepointStream {
 public:
  explicit RenderedCodepointStream(const std::string& source) : nfc_(source) {}

  bool next(MappedCodepoint& result) {
    for (;;) {
      if (emitSaraNikhahit_) {
        emitSaraNikhahit_ = false;
        result = {0x0E4D, saraAmSourceCodepointCount_};
        return true;
      }

      if (replayActive_) {
        MappedCodepoint replayed;
        if (!replay_.next(replayed)) {
          replayActive_ = false;
          continue;
        }

        if (replayMode_ == ReplayMode::SaraAmExpansion) {
          if (replayed.codepoint == 0x0E33) {
            replayActive_ = false;
            emitSaraAa_ = true;
            continue;
          }
          if (isThaiToneMark(replayed.codepoint)) {
            // Reordering makes these tones part of the rendered Sara Am
            // expansion. Keep the mapping monotonic at that boundary.
            replayed.sourceCodepointCount = saraAmSourceCodepointCount_;
            result = replayed;
            return true;
          }
          replayActive_ = false;
          result = replayed;
          return true;
        }

        if (isThaiToneMark(replayed.codepoint)) {
          result = replayed;
          return true;
        }
        replayActive_ = false;
        result = replayed;
        return true;
      }

      if (emitSaraAa_) {
        emitSaraAa_ = false;
        result = {0x0E32, saraAmSourceCodepointCount_};
        return true;
      }

      const NfcCodepointStream stateBeforeCodepoint = nfc_;
      MappedCodepoint codepoint;
      if (!nfc_.next(codepoint)) {
        if (hasPendingTones_) {
          replay_ = toneStart_;
          replayActive_ = true;
          replayMode_ = ReplayMode::FlushTones;
          hasPendingTones_ = false;
          continue;
        }
        return false;
      }

      if (isThaiToneMark(codepoint.codepoint)) {
        if (!hasPendingTones_) {
          toneStart_ = stateBeforeCodepoint;
        }
        hasPendingTones_ = true;
        continue;
      }

      if (hasPendingTones_) {
        replay_ = toneStart_;
        replayActive_ = true;
        replayMode_ = codepoint.codepoint == 0x0E33 ? ReplayMode::SaraAmExpansion : ReplayMode::FlushTones;
        if (codepoint.codepoint == 0x0E33) {
          saraAmSourceCodepointCount_ = codepoint.sourceCodepointCount;
          emitSaraNikhahit_ = true;
        }
        hasPendingTones_ = false;
        continue;
      }

      if (codepoint.codepoint == 0x0E33) {
        saraAmSourceCodepointCount_ = codepoint.sourceCodepointCount;
        emitSaraNikhahit_ = true;
        emitSaraAa_ = true;
        continue;
      }

      result = codepoint;
      return true;
    }
  }

  uint32_t sourceCodepointCount() const { return nfc_.sourceCodepointCount(); }

 private:
  enum class ReplayMode : uint8_t { FlushTones, SaraAmExpansion };

  NfcCodepointStream nfc_;
  NfcCodepointStream replay_ = nfc_;
  NfcCodepointStream toneStart_ = nfc_;
  uint32_t saraAmSourceCodepointCount_ = 0;
  ReplayMode replayMode_ = ReplayMode::FlushTones;
  bool hasPendingTones_ = false;
  bool replayActive_ = false;
  bool emitSaraNikhahit_ = false;
  bool emitSaraAa_ = false;
};

}  // namespace detail

inline uint32_t renderedCodepointsBeforeOffset(const std::string& renderedWord, const size_t renderedByteOffset) {
  const size_t clampedOffset = std::min(renderedByteOffset, renderedWord.size());
  const auto* renderedPtr = reinterpret_cast<const unsigned char*>(renderedWord.data());
  const auto* const renderedEnd = renderedPtr + clampedOffset;
  uint32_t renderedCount = 0;
  while (renderedPtr < renderedEnd) {
    const auto* const before = renderedPtr;
    utf8NextCodepoint(&renderedPtr);
    if (renderedPtr > renderedEnd || renderedPtr <= before) break;
    ++renderedCount;
  }
  return renderedCount;
}

inline uint32_t originalCodepointsBeforeRenderedOffset(const std::string& sourceWord, const std::string& renderedWord,
                                                       const size_t renderedByteOffset) {
  const uint32_t renderedCount = renderedCodepointsBeforeOffset(renderedWord, renderedByteOffset);
  if (renderedCount == 0) return 0;

  detail::RenderedCodepointStream stream(sourceWord);
  detail::MappedCodepoint codepoint;
  for (uint32_t i = 0; i < renderedCount; ++i) {
    if (!stream.next(codepoint)) {
      return stream.sourceCodepointCount();
    }
  }
  return codepoint.sourceCodepointCount;
}

// originalCodepointsBeforeRenderedOffset() for a series of ascending offsets into
// one word, walking the source and rendered spellings once for the whole series.
class SourceCodepointCounter {
 public:
  SourceCodepointCounter(const std::string& sourceWord, const std::string& renderedWord)
      : rendered_(renderedWord),
        renderedPtr_(reinterpret_cast<const unsigned char*>(renderedWord.data())),
        identical_(sourceWord == renderedWord),
        stream_(sourceWord) {}

  // `renderedByteOffset` must not decrease from one call to the next.
  uint32_t before(const size_t renderedByteOffset) {
    const auto* const end =
        reinterpret_cast<const unsigned char*>(rendered_.data()) + std::min(renderedByteOffset, rendered_.size());
    while (renderedPtr_ < end) {
      const auto* const codepointStart = renderedPtr_;
      utf8NextCodepoint(&renderedPtr_);
      if (renderedPtr_ > end || renderedPtr_ <= codepointStart) {
        renderedPtr_ = codepointStart;  // a later, larger offset may still take this codepoint
        break;
      }
      ++renderedCount_;
    }
    // Unchanged by normalization: every rendered codepoint is one source codepoint.
    if (identical_ || renderedCount_ == 0) return renderedCount_;
    while (!exhausted_ && streamed_ < renderedCount_) {
      if (!stream_.next(last_)) {
        exhausted_ = true;
        break;
      }
      ++streamed_;
    }
    return exhausted_ ? stream_.sourceCodepointCount() : last_.sourceCodepointCount;
  }

 private:
  const std::string& rendered_;
  const unsigned char* renderedPtr_;
  uint32_t renderedCount_ = 0;
  const bool identical_;
  detail::RenderedCodepointStream stream_;
  detail::MappedCodepoint last_ = {};
  uint32_t streamed_ = 0;
  bool exhausted_ = false;
};

// Source codepoints behind rendered text once its source spelling is gone, as when
// layout splits a token: a Sara Am expansion (Nikhahit, any tone marks, Sara Aa)
// came from one codepoint. Exact unless the source spelled Nikhahit + Sara Aa
// explicitly or held NFD sequences that NFC composed.
inline uint32_t sourceCodepointsInRendered(const std::string_view rendered) {
  const auto* p = reinterpret_cast<const unsigned char*>(rendered.data());
  const auto* const end = p + rendered.size();
  uint32_t count = 0;
  while (p < end) {
    if (utf8NextCodepoint(&p) == 0x0E4D) {
      const auto* next = p;
      uint32_t following;
      do {
        following = next < end ? utf8NextCodepoint(&next) : 0;
      } while (detail::isThaiToneMark(following));
      if (following == 0x0E32) continue;  // counted with its Sara Aa
    }
    ++count;
  }
  return count;
}

}  // namespace VisibleTextOffset
