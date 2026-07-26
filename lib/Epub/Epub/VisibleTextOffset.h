#pragma once

#include <Utf8.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace VisibleTextOffset {

inline uint32_t originalCodepointsBeforeRenderedOffset(const std::string& sourceWord,
                                                       const std::string& renderedWord,
                                                       const size_t renderedByteOffset) {
  const size_t clampedOffset = std::min(renderedByteOffset, renderedWord.size());
  const auto* renderedPtr = reinterpret_cast<const unsigned char*>(renderedWord.data());
  const auto* const renderedEnd = renderedPtr + clampedOffset;
  uint32_t renderedCount = 0;
  while (renderedPtr < renderedEnd) {
    utf8NextCodepoint(&renderedPtr);
    renderedCount++;
  }

  const auto* sourcePtr = reinterpret_cast<const unsigned char*>(sourceWord.c_str());
  const auto* const sourceEnd = sourcePtr + sourceWord.size();
  uint32_t sourceCount = 0;
  uint32_t saraAmCount = 0;
  while (sourcePtr < sourceEnd && sourceCount + saraAmCount < renderedCount) {
    const uint32_t cp = utf8NextCodepoint(&sourcePtr);
    sourceCount++;
    if (cp == 0x0E33) {
      saraAmCount++;
    }
  }
  return sourceCount;
}

}  // namespace VisibleTextOffset
