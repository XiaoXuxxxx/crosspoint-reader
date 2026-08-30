#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Utf8.h"
#include "lib/Epub/Epub/VisibleTextOffset.h"
#include "lib/Epub/Epub/thai/ThaiSegmenter.h"

namespace {

constexpr char K_THAI_SOURCE[] = "\xE0\xB8\x81\xE0\xB8\xB3" "A";            // กำA
constexpr char K_EXPLICIT_DECOMPOSED[] = "\xE0\xB8\x81\xE0\xB9\x8D\xE0\xB8\xB2" "A";  // กํA
constexpr char K_THAI_MULTIWORD[] = "\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\xA9\xE0\xB8\xB2"
                                    "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2";  // ภาษาไทย
constexpr char K_THAI_LANGUAGE[] = "\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\xA9\xE0\xB8\xB2";  // ภาษา

bool containsBreak(const std::vector<size_t>& breaks, const size_t offset) {
  return std::find(breaks.begin(), breaks.end(), offset) != breaks.end();
}

}  // namespace

TEST(ThaiSegmentation, SaraAmExpansionIsOneAtomicCluster) {
  const std::string source = K_THAI_SOURCE;
  const std::string rendered = utf8DecomposeThaiSaraAm(source);
  const auto breaks = thaiWordBreakByteOffsets(rendered);

  // ก + U+0E4D occupies six bytes; U+0E32 starts at byte six and must not be
  // a token start. The legal boundary after the complete expansion is byte 9.
  EXPECT_FALSE(containsBreak(breaks, 6));
  EXPECT_TRUE(containsBreak(breaks, 9));
  EXPECT_EQ(VisibleTextOffset::originalCodepointsBeforeRenderedOffset(source, rendered, 3), 1U);
  EXPECT_EQ(VisibleTextOffset::originalCodepointsBeforeRenderedOffset(source, rendered, 9), 2U);
}

TEST(ThaiSegmentation, ExplicitNikhahitSaraAaPairIsAlsoAtomic) {
  const std::string source = K_EXPLICIT_DECOMPOSED;
  const auto breaks = thaiWordBreakByteOffsets(source);

  EXPECT_FALSE(containsBreak(breaks, 6));
  EXPECT_TRUE(containsBreak(breaks, 9));
  EXPECT_EQ(VisibleTextOffset::originalCodepointsBeforeRenderedOffset(source, source, 9), 3U);
}

TEST(ThaiSegmentation, LexiconFindsTheTwoWordsInThai) {
  const std::string text = K_THAI_MULTIWORD;
  const auto breaks = thaiWordBreakByteOffsets(text);

  // ภาษา and ไทย are both lexicon entries. Maximal matching must not fall
  // back to one break per Thai cluster.
  EXPECT_EQ(breaks, (std::vector<size_t>{12}));
}

TEST(ThaiSegmentation, NfcContractionPreservesThaiVisibleOffset) {
  const std::string source = std::string("a\xCC\x81") + K_THAI_LANGUAGE;
  const std::string rendered = utf8DecomposeThaiSaraAm(utf8ComposeNfc(source));
  const std::string expected = std::string("\xC3\xA1") + K_THAI_LANGUAGE;

  EXPECT_EQ(rendered, expected);
  const auto breaks = thaiWordBreakByteOffsets(rendered);
  EXPECT_EQ(breaks, (std::vector<size_t>{2}));
  EXPECT_EQ(VisibleTextOffset::originalCodepointsBeforeRenderedOffset(source, rendered, breaks.front()), 2U);
  EXPECT_EQ(VisibleTextOffset::originalCodepointsBeforeRenderedOffset(source, rendered, rendered.size()), 6U);
}
