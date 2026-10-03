#include <gtest/gtest.h>

#include "lib/EpdFont/EpdFontData.h"
#include "lib/EpdFont/builtinFonts/notosans_8_regular.h"
#include "lib/EpdFont/builtinFonts/ubuntu_10_bold.h"
#include "lib/EpdFont/builtinFonts/ubuntu_10_regular.h"
#include "lib/EpdFont/builtinFonts/ubuntu_12_bold.h"
#include "lib/EpdFont/builtinFonts/ubuntu_12_regular.h"

// ============================================================================
// Anchor selection and placement for combining marks without GPOS tables.
//
// Hebrew niqqud whose identity depends on position must not use the default
// centre-and-raise heuristic: dagesh sits inside the letter body, the
// shin/sin dots sit over the letter's right/left arm, and holam hangs over
// the left corner (see PR #2541 review feedback).
// ============================================================================

using combiningMark::Anchor;
using combiningMark::anchorFor;
using combiningMark::anchorOver;
using combiningMark::anchorOverRotated90CW;
using combiningMark::raiseAboveBase;
using combiningMark::raisedMarkTop;

TEST(AnchorFor, PositionSensitiveNiqqud) {
  EXPECT_EQ(anchorFor(0x05BC), Anchor::CenterNative);  // dagesh/mapiq
  EXPECT_EQ(anchorFor(0x05BA), Anchor::CenterNative);  // holam haser for vav
  EXPECT_EQ(anchorFor(0x05C1), Anchor::RightNative);   // shin dot
  EXPECT_EQ(anchorFor(0x05C2), Anchor::LeftNative);    // sin dot
  EXPECT_EQ(anchorFor(0x05B9), Anchor::LeftNative);    // holam
}

TEST(AnchorFor, EverythingElseKeepsCentreRaisedDefault) {
  EXPECT_EQ(anchorFor(0x05B0), Anchor::CenterRaised);  // Hebrew sheva
  EXPECT_EQ(anchorFor(0x05B7), Anchor::CenterRaised);  // Hebrew patach
  EXPECT_EQ(anchorFor(0x064E), Anchor::CenterRaised);  // Arabic fatha
  EXPECT_EQ(anchorFor(0x0651), Anchor::CenterRaised);  // Arabic shadda
  EXPECT_EQ(anchorFor(0x0301), Anchor::CenterRaised);  // combining acute
}

// Base glyph: cursor 100, left 1, width 12.  Mark: left 2, width 4.
TEST(AnchorOver, HorizontalPlacementPerAnchor) {
  // Centered: base bitmap spans [101, 113), centre 107; mark starts at 105.
  EXPECT_EQ(anchorOver(Anchor::CenterRaised, 100, 1, 12, 2, 4), 105 - 2);
  EXPECT_EQ(anchorOver(Anchor::CenterNative, 100, 1, 12, 2, 4), 105 - 2);
  // Left-aligned: mark bitmap starts at the base bitmap's left edge (101).
  EXPECT_EQ(anchorOver(Anchor::LeftNative, 100, 1, 12, 2, 4), 101 - 2);
  // Right-aligned: mark bitmap ends at the base bitmap's right edge (113).
  EXPECT_EQ(anchorOver(Anchor::RightNative, 100, 1, 12, 2, 4), 109 - 2);
}

// The rotated coordinate system inverts every left/width term, so the mark's
// offset from the base cursor must be the exact mirror of the unrotated one.
TEST(AnchorOverRotated90CW, MirrorsUnrotatedOffsets) {
  for (const Anchor anchor : {Anchor::CenterRaised, Anchor::CenterNative, Anchor::LeftNative, Anchor::RightNative}) {
    const int offset = anchorOver(anchor, 100, 1, 12, 2, 4) - 100;
    EXPECT_EQ(anchorOverRotated90CW(anchor, 100, 1, 12, 2, 4), 100 - offset);
  }
}

TEST(RaiseAboveBase, NativeAnchorsKeepFontDesignedHeight) {
  // A dagesh-like dot designed inside the letter body (top 8 of a base whose
  // top is 12) must NOT be hoisted above the letter.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterNative, 8, 3, 12), 0);
  // Shin/sin dots overlapping the letter's top must not be pushed clear.
  EXPECT_EQ(raiseAboveBase(Anchor::RightNative, 13, 3, 12), 0);
  EXPECT_EQ(raiseAboveBase(Anchor::LeftNative, 13, 3, 12), 0);
}

TEST(RaiseAboveBase, CentreRaisedBehaviourUnchanged) {
  // Above-baseline mark colliding with the base: raised to restore a 1px gap.
  // gap = markTop - markHeight - baseTop = 10 - 3 - 12 = -5  ->  raise 6.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 10, 3, 12), 6);
  // Already clear of the base: no raise.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 16, 3, 12), 0);
  // Below-baseline mark (kasra, cedilla): stays at font-native position.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 2, 4, 12), 0);
}

TEST(RaisedMarkTop, RaiseIncreasesEffectiveTop) {
  // A mark raised by 3px sits 3px HIGHER (larger value, same "larger = higher"
  // convention as markTop/baseTop) than its font-native top, not lower.
  EXPECT_EQ(raisedMarkTop(18, 3), 21);
  EXPECT_EQ(raisedMarkTop(18, 0), 18);
}

// Regression test for the ฟ/ป/ฝ/ฬ ("ascender"/right-tail consonant) mark-collision bug:
// a second above-mark (e.g. a tone mark) stacked on a first above-mark (e.g. an above-vowel)
// must reference the first mark's RAISED top, not its font-native or wrongly-lowered position,
// or the two marks overlap. This only surfaces when the first mark needed a nonzero raise, as
// on an ascender consonant whose mark has no room beside the tail and clears it from above.
// Metrics below are the real merged NotoSansThai glyphs from ubuntu_10_regular.h: U+0E1F ฟ
// (top 16), U+0E35 ี (top 17, height 4), U+0E48 ่ (top 18, height 5).
TEST(ThaiMarkStacking, ToneMarkClearsVowelMarkOnAscenderConsonant) {
  constexpr int baseTop = 16;                      // U+0E1F ฟ
  constexpr int saraIiTop = 17, saraIiHeight = 4;  // U+0E35 ี
  constexpr int maiEkTop = 18, maiEkHeight = 5;    // U+0E48 ่

  const int raise1 = raiseAboveBase(Anchor::CenterRaised, saraIiTop, saraIiHeight, baseTop);
  const int stackBase = raisedMarkTop(saraIiTop, raise1);
  // Independently-verified true top of the raised vowel mark (17 + 4).
  constexpr int expectedVowelFinalTop = 21;
  EXPECT_EQ(stackBase, expectedVowelFinalTop);

  const int raise2 = raiseAboveBase(Anchor::CenterRaised, maiEkTop, maiEkHeight, stackBase);
  const int toneFinalTop = raisedMarkTop(maiEkTop, raise2);
  const int toneFinalBottom = toneFinalTop - maiEkHeight + 1;

  // The tone mark's bottom edge must clear the vowel mark's true top edge, not merely
  // whatever (possibly wrong) reference point was used to compute its own raise.
  EXPECT_GT(toneFinalBottom, expectedVowelFinalTop);
}

// ============================================================================
// Thai mark placement against the real glyphs of the built-in UI fonts.
// ============================================================================

namespace {

using combiningMark::findThaiAscenderTail;
using combiningMark::isThaiAscenderConsonant;
using combiningMark::lowerBelowBase;
using combiningMark::thaiAscenderMarkShift;

const EpdGlyph& glyphOf(const EpdFontData& font, const uint32_t cp) {
  for (uint32_t i = 0; i < font.intervalCount; i++) {
    const EpdUnicodeInterval& interval = font.intervals[i];
    if (cp >= interval.first && cp <= interval.last) return font.glyph[interval.offset + (cp - interval.first)];
  }
  ADD_FAILURE() << "no glyph for U+" << std::hex << cp;
  return font.glyph[0];
}

combiningMark::ThaiAscenderTail tailOf(const EpdFontData& font, const uint32_t cp) {
  const EpdGlyph& g = glyphOf(font, cp);
  return findThaiAscenderTail(&font.bitmap[g.dataOffset], g.width, g.height, g.top, font.is2Bit);
}

int bottomOf(const EpdGlyph& g) { return g.top - g.height; }

}  // namespace

TEST(LowerBelowBase, LeavesOneBlankRowUnderTheBase) {
  const EpdFontData& font = ubuntu_10_regular;
  const int saraU = glyphOf(font, 0x0E38).top;   // ุ
  const int saraUu = glyphOf(font, 0x0E39).top;  // ู
  // ก ends on the baseline: ุ already has one blank row above it.
  EXPECT_EQ(lowerBelowBase(saraU, bottomOf(glyphOf(font, 0x0E01))), 0);
  // ฎ ฏ ฐ descend to row -4 and ญ to row -3: drop the vowel below the tail.
  EXPECT_EQ(lowerBelowBase(saraU, bottomOf(glyphOf(font, 0x0E0E))), 5);
  EXPECT_EQ(lowerBelowBase(saraUu, bottomOf(glyphOf(font, 0x0E0F))), 5);
  EXPECT_EQ(lowerBelowBase(saraU, bottomOf(glyphOf(font, 0x0E10))), 5);
  EXPECT_EQ(lowerBelowBase(saraUu, bottomOf(glyphOf(font, 0x0E0D))), 4);
  // Phinthu (level 2) keeps two blank rows.
  EXPECT_EQ(lowerBelowBase(glyphOf(font, 0x0E3A).top, bottomOf(glyphOf(font, 0x0E01)), 2), 1);
}

TEST(LowerBelowBase, NeverOverlapsTheBaseInAnyUiFont) {
  for (const EpdFontData* font :
       {&ubuntu_10_regular, &ubuntu_10_bold, &ubuntu_12_regular, &ubuntu_12_bold, &notosans_8_regular}) {
    for (uint32_t base = 0x0E01; base <= 0x0E2E; base++) {
      for (const uint32_t mark : {0x0E38U, 0x0E39U, 0x0E3AU}) {
        const EpdGlyph& b = glyphOf(*font, base);
        const EpdGlyph& m = glyphOf(*font, mark);
        const int markTop = m.top - lowerBelowBase(m.top, bottomOf(b));
        EXPECT_LT(markTop, bottomOf(b)) << "U+" << std::hex << base << " U+" << mark;
      }
    }
  }
}

TEST(ThaiAscenderTail, OnlyTheFourTailedConsonants) {
  for (uint32_t cp = 0x0E01; cp <= 0x0E2E; cp++) {
    EXPECT_EQ(isThaiAscenderConsonant(cp), cp == 0x0E1B || cp == 0x0E1D || cp == 0x0E1F || cp == 0x0E2C)
        << std::hex << cp;
  }
}

TEST(ThaiAscenderTail, FindsTheTailAndTheBodyBelowIt) {
  const EpdFontData& font = ubuntu_10_regular;
  // ป: tail in columns 8-9 rising from the body at row 12 (ก's top) to row 16.
  EXPECT_EQ(tailOf(font, 0x0E1B).bodyTop, 12);
  EXPECT_EQ(tailOf(font, 0x0E1B).tailLeft, 8);
  EXPECT_EQ(tailOf(font, 0x0E1F).bodyTop, 12);
  EXPECT_EQ(tailOf(font, 0x0E1F).tailLeft, 11);
  // ฬ's tail curls back over the body at row 13; marks must clear that row too.
  EXPECT_EQ(tailOf(font, 0x0E2C).bodyTop, 13);
  EXPECT_EQ(tailOf(font, 0x0E2C).tailLeft, 11);
}

TEST(ThaiAscenderTail, BodyMeetsRegularConsonantHeightInEveryUiFont) {
  for (const EpdFontData* font :
       {&ubuntu_10_regular, &ubuntu_10_bold, &ubuntu_12_regular, &ubuntu_12_bold, &notosans_8_regular}) {
    const int xHeight = glyphOf(*font, 0x0E01).top;  // ก
    for (const uint32_t cp : {0x0E1BU, 0x0E1DU, 0x0E1FU, 0x0E2CU}) {
      const EpdGlyph& g = glyphOf(*font, cp);
      const auto tail = tailOf(*font, cp);
      EXPECT_LT(tail.bodyTop, g.top) << std::hex << cp;
      EXPECT_GE(tail.bodyTop, xHeight - 1) << std::hex << cp;
      EXPECT_LE(tail.bodyTop, xHeight + 2) << std::hex << cp;
      EXPECT_GT(tail.tailLeft, g.width / 2) << std::hex << cp;  // the tail is on the right
    }
  }
}

TEST(ThaiAscenderTail, ReadsTwoBitBitmaps) {
  // 4x4, 2bpp: a 1-column tail at x=3 above a full-width body from row 2 down.
  //   . . . 3
  //   . . 1 3     <- light gray still counts as ink
  //   3 3 3 3
  //   3 3 3 3
  constexpr uint8_t bitmap[] = {0x03, 0x07, 0xFF, 0xFF};
  const auto tail = findThaiAscenderTail(bitmap, 4, 4, 10, true);
  EXPECT_EQ(tail.bodyTop, 8);
  EXPECT_EQ(tail.tailLeft, 2);
}

TEST(ThaiAscenderMarkShift, MovesTheMarkLeftOfTheTail) {
  // ปี at ubuntu_10_regular, ป drawn at x = 0: ี spans 2..11, the tail starts at 9.
  const auto s = thaiAscenderMarkShift(2, 11, 9, /*minLeft=*/-3);
  EXPECT_EQ(s.shiftX, -4);
  EXPECT_TRUE(s.clearsTail);
}

TEST(ThaiAscenderMarkShift, NeverMovesRightOrPastTheBound) {
  // Already clear of the tail.
  EXPECT_EQ(thaiAscenderMarkShift(0, 5, 9, -10).shiftX, 0);
  // Bounded by the previous cluster: the mark cannot get clear.
  const auto s = thaiAscenderMarkShift(2, 11, 9, /*minLeft=*/1);
  EXPECT_EQ(s.shiftX, -1);
  EXPECT_FALSE(s.clearsTail);
  // A bound right of the mark never pushes it right.
  EXPECT_EQ(thaiAscenderMarkShift(2, 11, 9, 5).shiftX, 0);
}

// ปี, ป่ and ปั: once moved left of the tail, the first mark keeps the height it has over
// a regular consonant instead of being lifted over the tail top (16).
TEST(ThaiAscenderMarkShift, MarkKeepsBodyHeightOnceClearOfTheTail) {
  const EpdFontData& font = ubuntu_10_regular;
  const EpdGlyph& pa = glyphOf(font, 0x0E1B);
  const auto tail = tailOf(font, 0x0E1B);
  const int penX = fp4::toPixel(pa.advanceX);  // ป drawn at x = 0
  for (const uint32_t cp : {0x0E35U, 0x0E48U, 0x0E31U}) {
    const EpdGlyph& m = glyphOf(font, cp);
    const int markLeft = penX + m.left;
    const auto s =
        thaiAscenderMarkShift(markLeft, markLeft + m.width - 1, pa.left + tail.tailLeft, pa.left - m.width / 2);
    EXPECT_TRUE(s.clearsTail) << std::hex << cp;
    EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, m.top, m.height, tail.bodyTop),
              raiseAboveBase(Anchor::CenterRaised, m.top, m.height, glyphOf(font, 0x0E01).top))
        << std::hex << cp;
  }
}
