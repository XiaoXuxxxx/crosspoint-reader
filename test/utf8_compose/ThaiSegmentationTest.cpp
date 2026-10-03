#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "Utf8.h"
#include "lib/Epub/Epub/VisibleTextOffset.h"
#include "lib/Epub/Epub/thai/ThaiLexicon.h"
#include "lib/Epub/Epub/thai/ThaiSegmenter.h"
#include "lib/Epub/Epub/thai/generated/thaiLexicon.generated.h"

namespace {

constexpr char K_THAI_SOURCE[] =
    "\xE0\xB8\x81\xE0\xB8\xB3"
    "A";  // กำA
constexpr char K_EXPLICIT_DECOMPOSED[] =
    "\xE0\xB8\x81\xE0\xB9\x8D\xE0\xB8\xB2"
    "A";  // กํA
constexpr char K_THAI_MULTIWORD[] =
    "\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\xA9\xE0\xB8\xB2"
    "\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2";                                             // ภาษาไทย
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

TEST(VisibleTextOffsetMapping, NfdHangulSyllableCountsItsSourceJamo) {
  // ᄒ + ᅡ + ᆫ compose to one rendered 한, so the Thai word after it starts
  // three source codepoints in.
  const std::string source = std::string("\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB") + K_THAI_LANGUAGE;
  const std::string rendered = utf8DecomposeThaiSaraAm(utf8ComposeNfc(source));

  EXPECT_EQ(rendered, std::string("\xED\x95\x9C") + K_THAI_LANGUAGE);
  EXPECT_EQ(VisibleTextOffset::originalCodepointsBeforeRenderedOffset(source, rendered, 3), 3U);
}

namespace {

std::vector<std::string> thaiTokens(const std::string& text) {
  std::vector<std::string> tokens;
  size_t start = 0;
  for (const size_t offset : thaiWordBreakByteOffsets(text)) {
    tokens.push_back(text.substr(start, offset - start));
    start = offset;
  }
  tokens.push_back(text.substr(start));
  return tokens;
}

using Tokens = std::vector<std::string>;

}  // namespace

TEST(ThaiSegmentation, RepetitionAndAbbreviationSignsStayOnTheirWord) {
  EXPECT_EQ(thaiTokens("เด็กๆ"), Tokens{"เด็กๆ"});
  EXPECT_EQ(thaiTokens("มากๆ"), Tokens{"มากๆ"});
  EXPECT_EQ(thaiTokens("กรุงเทพฯ"), Tokens{"กรุงเทพฯ"});
  // ฯลฯ is one unit wherever it stands.
  EXPECT_EQ(thaiTokens("ฯลฯ"), Tokens{"ฯลฯ"});
  EXPECT_EQ(thaiTokens("ผลไม้ฯลฯ"), Tokens{"ผลไม้ฯลฯ"});
  EXPECT_EQ(thaiTokens("ไม่ปิดฯลฯครับ"), (Tokens{"ไม่", "ปิดฯลฯ", "ครับ"}));
}

TEST(ThaiSegmentation, FollowingVowelsStayInTheirSyllable) {
  // Unknown names fall back to one token per cluster, never one per spacing vowel.
  EXPECT_EQ(thaiTokens("โนบิตะ"), (Tokens{"โน", "บิ", "ตะ"}));
  EXPECT_EQ(thaiTokens("โดราเอมอน"), (Tokens{"โด", "รา", "เอม", "อน"}));
  EXPECT_EQ(thaiTokens(utf8DecomposeThaiSaraAm("กำลังรอการอนุญาต")),
            (Tokens{utf8DecomposeThaiSaraAm("กำลัง"), "รอ", "การ", "อนุญาต"}));
}

TEST(ThaiSegmentation, DictionaryWordsSplitAsBefore) {
  EXPECT_EQ(thaiTokens("วันนี้อากาศดีมาก"), (Tokens{"วันนี้", "อากาศ", "ดี", "มาก"}));
  EXPECT_EQ(thaiTokens("ภาษาไทย"), (Tokens{"ภาษา", "ไทย"}));
}

TEST(ThaiSegmentation, ThaiDigitsStayTogether) { EXPECT_EQ(thaiTokens("ปี๒๕๖๗"), (Tokens{"ปี", "๒๕๖๗"})); }

TEST(ThaiSegmentation, NoTokenStartsWithAMarkOrANonStarter) {
  for (const char* text : {"เด็กๆเล่นกันสนุกมากๆ", "กรุงเทพฯเป็นเมืองหลวง", "น้ำใจดีมาก", "สั้นๆ", "เกาะกลางน้ำ"}) {
    const auto tokens = thaiTokens(utf8DecomposeThaiSaraAm(text));
    for (size_t i = 1; i < tokens.size(); i++) {
      const auto* p = reinterpret_cast<const unsigned char*>(tokens[i].c_str());
      const uint32_t first = utf8NextCodepoint(&p);
      EXPECT_FALSE(utf8IsCombiningMark(first) || utf8IsThaiNonStarter(first)) << text << " token " << i;
    }
  }
}

TEST(VisibleTextOffsetMapping, CounterMatchesPerOffsetMappingAtEveryBoundary) {
  // Codepoints that change between source and rendered text: Sara Am and the tones it
  // reorders, an explicit Nikhahit, NFD Latin and Hangul jamo, plus plain Thai, CJK and ASCII.
  const uint32_t alphabet[] = {0x0E01, 0x0E19, 0x0E33, 0x0E48, 0x0E49, 0x0E4D, 0x0E32, 0x0E34,
                               0x0061, 0x0301, 0x0065, 0x1100, 0x1161, 0x11AB, 0x4E2D, 0x0020};
  uint32_t seed = 12345;
  const auto random = [&seed](const uint32_t bound) {
    seed = seed * 1103515245U + 12345U;
    return (seed >> 16) % bound;
  };
  for (int round = 0; round < 3000; round++) {
    std::string source;
    const uint32_t length = 1 + random(12);
    for (uint32_t i = 0; i < length; i++) utf8AppendCodepoint(alphabet[random(16)], source);
    const std::string rendered = utf8DecomposeThaiSaraAm(utf8ComposeNfc(source));
    VisibleTextOffset::SourceCodepointCounter counter(source, rendered);
    for (size_t offset = 0; offset <= rendered.size(); offset++) {
      if (offset < rendered.size() && (static_cast<unsigned char>(rendered[offset]) & 0xC0) == 0x80) continue;
      ASSERT_EQ(counter.before(offset),
                VisibleTextOffset::originalCodepointsBeforeRenderedOffset(source, rendered, offset))
          << "source " << source << " offset " << offset;
    }
  }
}

TEST(VisibleTextOffsetMapping, RenderedTextCountsSaraAmOnce) {
  EXPECT_EQ(VisibleTextOffset::sourceCodepointsInRendered(utf8DecomposeThaiSaraAm("น้ำค้าง")), 7U);
  EXPECT_EQ(VisibleTextOffset::sourceCodepointsInRendered(utf8DecomposeThaiSaraAm("กำ")), 2U);
  // A Nikhahit without Sara Aa after it is a codepoint of its own (Pali/Sanskrit spelling).
  EXPECT_EQ(VisibleTextOffset::sourceCodepointsInRendered("สํ"), 2U);
  EXPECT_EQ(VisibleTextOffset::sourceCodepointsInRendered("abc"), 3U);
}

// Decode every front-coded entry independently and check the buffer-free lookup
// against an exact set: all entries are found, near misses are not.
TEST(ThaiLexicon, LookupIsExactForEveryEntryAndItsNearMisses) {
  const ThaiLexicon& lexicon = thai_lexicon;
  const auto readU32 = [&](const size_t offset) {
    uint32_t value;
    std::memcpy(&value, lexicon.data + offset, sizeof(value));
    return value;
  };
  std::vector<std::string> entries;
  for (uint32_t block = 0; block < lexicon.restartCount; block++) {
    size_t p = readU32(4 + block * 4);
    const size_t end = block + 1 < lexicon.restartCount ? readU32(4 + (block + 1) * 4) : lexicon.size;
    std::string entry;
    for (bool first = true; p < end; first = false) {
      const size_t shared = first ? 0 : lexicon.data[p++];
      const size_t suffix = lexicon.data[p++];
      entry = entry.substr(0, shared) + std::string(reinterpret_cast<const char*>(lexicon.data + p), suffix);
      p += suffix;
      entries.push_back(entry);
    }
  }
  ASSERT_EQ(entries.size(), 12000U);
  const std::set<std::string> exact(entries.begin(), entries.end());
  const auto contains = [&](const std::string& s) { return thaiLexiconContains(lexicon, s.data(), s.size()); };
  for (const std::string& entry : entries) {
    ASSERT_TRUE(contains(entry)) << entry;
    for (const std::string& probe : {entry.substr(0, entry.size() - 1), entry + "\xE0\xB8\x81", entry + "a",
                                     entry.substr(0, entry.size() - 1) + static_cast<char>(entry.back() + 1)}) {
      ASSERT_EQ(contains(probe), exact.count(probe) == 1) << probe;
    }
  }
}
