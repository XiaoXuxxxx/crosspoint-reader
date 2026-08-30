#include <gtest/gtest.h>

#include "lib/EpdFont/EpdFontData.h"

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
// or the two marks overlap. This only surfaces when the first mark needed a nonzero raise,
// which only happens for ascender consonants (their font-authored top exceeds what the vowel
// mark's own native position assumes). Metrics below are the real merged NotoSansThai glyphs
// from ubuntu_10_regular.h: U+0E1F ฟ (top 16), U+0E35 ี (top 18, height 4), U+0E48 ่ (top 18,
// height 5).
TEST(ThaiMarkStacking, ToneMarkClearsVowelMarkOnAscenderConsonant) {
  constexpr int baseTop = 16;                      // U+0E1F ฟ
  constexpr int saraIiTop = 18, saraIiHeight = 4;  // U+0E35 ี
  constexpr int maiEkTop = 18, maiEkHeight = 5;    // U+0E48 ่

  const int raise1 = raiseAboveBase(Anchor::CenterRaised, saraIiTop, saraIiHeight, baseTop);
  const int stackBase = raisedMarkTop(saraIiTop, raise1);
  // Independently-verified true top of the raised vowel mark (18 + 3).
  constexpr int expectedVowelFinalTop = 21;
  EXPECT_EQ(stackBase, expectedVowelFinalTop);

  const int raise2 = raiseAboveBase(Anchor::CenterRaised, maiEkTop, maiEkHeight, stackBase);
  const int toneFinalTop = raisedMarkTop(maiEkTop, raise2);
  const int toneFinalBottom = toneFinalTop - maiEkHeight + 1;

  // The tone mark's bottom edge must clear the vowel mark's true top edge, not merely
  // whatever (possibly wrong) reference point was used to compute its own raise.
  EXPECT_GT(toneFinalBottom, expectedVowelFinalTop);
}
