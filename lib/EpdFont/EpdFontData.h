// From
// https://github.com/vroland/epdiy/blob/c61e9e923ce2418150d54f88cea5d196cdc40c54/src/epd_internals.h

#pragma once
#include <cstdint>

/// Font metrics use "fixed-point 4" (4 fractional bits, i.e. 1/16-pixel
/// resolution).  Both the 12.4 glyph advances (uint16_t) and the 4.4 kern
/// values (int8_t) share the same 4 fractional bits, so they can be freely
/// added before snapping to whole pixels.
///
/// Rendering and measurement use "differential rounding": each glyph step
/// (previous advance + current kern) is combined in fixed-point and snapped
/// to a pixel as one unit.  This guarantees identical character pairs always
/// produce the same pixel spacing, regardless of position on the line.
///
/// The helpers below eliminate the raw bit-shifts that would otherwise be
/// scattered across every layout / measurement call site.
namespace fp4 {
constexpr int FRAC_BITS = 4;
constexpr int32_t HALF = 1 << (FRAC_BITS - 1);  // 8, added before shift for round-to-nearest

/// Convert an integer pixel value to 12.4 fixed-point.
constexpr int32_t fromPixel(int px) { return static_cast<int32_t>(px) << FRAC_BITS; }

/// Snap a fixed-point value to the nearest integer pixel.
constexpr int toPixel(int32_t fp) { return static_cast<int>((fp + HALF) >> FRAC_BITS); }

/// Convert a fixed-point value to float (mainly useful for debug logging).
constexpr float toFloat(int32_t fp) { return fp / static_cast<float>(1 << FRAC_BITS); }
}  // namespace fp4

/// Helpers for positioning Unicode combining marks (U+0300 ff.) over a
/// preceding base glyph without GPOS anchor tables.
namespace combiningMark {

constexpr int MIN_GAP_PX = 1;

/// Compute the cursor-X at which to render a combining mark so its bitmap
/// is visually centered over the base glyph's bitmap.
constexpr int centerOver(int baseCursorPos, int baseLeft, int baseWidth, int markLeft, int markWidth) {
  return baseCursorPos + baseLeft + baseWidth / 2 - markWidth / 2 - markLeft;
}

/// Rotated-90CW variant of centerOver.  In the rotated coordinate system
/// renderCharImpl uses (cursorY - left) instead of (cursorX + left), so
/// every left/width term inverts sign.
constexpr int centerOverRotated90CW(int baseCursorPos, int baseLeft, int baseWidth, int markLeft, int markWidth) {
  return baseCursorPos - baseLeft - baseWidth / 2 + markWidth / 2 + markLeft;
}

/// For combining marks that sit entirely above the baseline, compute how many
/// pixels to raise the mark so there is at least MIN_GAP_PX between its bottom
/// edge and the top of the base glyph.  Returns 0 for marks that extend to or
/// below the baseline (e.g. cedilla, dot-below, ogonek).
/// `level` defaults to 1 for backward compatibility with existing Hebrew/Latin
/// marks; Thai above-marks pass their level (1+) for proper stacking height.
constexpr int raiseAboveBase(int markTop, int markHeight, int baseTop, int level = 1) {
  if (markTop - markHeight <= 0) return 0;
  const int gap = markTop - markHeight - baseTop;
  const int minGap = MIN_GAP_PX * level;
  return (gap < minGap) ? (minGap - gap) : 0;
}

/// Mirror of raiseAboveBase for marks that sit below the baseline (Thai below-vowels).
/// Computes how many pixels to lower the mark so there is at least
/// MIN_GAP_PX * level between the mark's top edge and the base glyph's bottom edge.
/// `baseBottom` is the base glyph's bottom edge (baseTop - baseHeight, negative for descenders).
/// `markTop` is the mark glyph's top (typically near 0 for below-base marks).
/// `level` is 1 for Below1, 2 for Below2.
constexpr int lowerBelowBase(int markTop, int baseBottom, int level = 1) {
  const int gap = markTop - baseBottom;
  const int minGap = MIN_GAP_PX * level;
  return (gap < minGap) ? (minGap - gap) : 0;
}

}  // namespace combiningMark

// Thai consonant class for mark positioning, aligned with libthai (thctype.c).
// Ascenders (overshoot) have a tall left stem — above-marks shift LEFT to clear it.
// Descenders (undershoot) extend below baseline — below-marks need extra lowering.
// Undersplit consonants have a split part below baseline — below-marks need extra
// lowering like descenders, and the split tail may need special handling.
enum class ThaiConsonantClass : uint8_t {
  Regular = 0,
  Ascender,    // ป ฝ ฟ ฬ (U+0E1B, U+0E1D, U+0E1F, U+0E2C) — tall left stem
  Descender,   // ฎ ฏ (U+0E0E, U+0E0F) — extends below baseline
  Undersplit,  // ญ ฐ (U+0E0D, U+0E10) — split part below baseline
};

constexpr ThaiConsonantClass thaiConsonantClass(const uint32_t cp) {
  // Ascenders (overshoot): ป (U+0E1B), ฝ (U+0E1D), ฟ (U+0E1F), ฬ (U+0E2C)
  if (cp == 0x0E1B || cp == 0x0E1D || cp == 0x0E1F || cp == 0x0E2C) return ThaiConsonantClass::Ascender;
  // Descenders (undershoot): ฎ (U+0E0E), ฏ (U+0E0F)
  if (cp == 0x0E0E || cp == 0x0E0F) return ThaiConsonantClass::Descender;
  // Undersplit: ญ (U+0E0D), ฐ (U+0E10) — split tail below baseline
  if (cp == 0x0E0D || cp == 0x0E10) return ThaiConsonantClass::Undersplit;
  return ThaiConsonantClass::Regular;
}

namespace combiningMark {

/// Horizontal shift for Thai above-marks on ascender consonants.
/// Ascenders (ป ฝ ฟ ฬ) have a tall left stem; shift the mark LEFT so it
/// doesn't overlap the stem.  Returns 0 for non-ascender bases.
/// Aligned with libthai thrend.c: ascender consonants use shiftleft glyph variant.
constexpr int thaiAboveMarkShiftX(const uint32_t baseCp, const int markWidth) {
  if (thaiConsonantClass(baseCp) == ThaiConsonantClass::Ascender) {
    return -(markWidth / 4);  // shift LEFT by ~25% of mark width
  }
  return 0;
}

}  // namespace combiningMark

/// Fixed-point conventions used by EpdGlyph and EpdFontData:
///   advanceX:   12.4 unsigned fixed-point in uint16_t  (use fp4::toPixel)
///   kernMatrix:  4.4 signed fixed-point in int8_t      (use fp4::toPixel)
/// Both share 4 fractional bits so they combine directly in an accumulator.

/// Font data stored PER GLYPH
typedef struct {
  uint8_t width;        ///< Bitmap dimensions in pixels
  uint8_t height;       ///< Bitmap dimensions in pixels
  uint16_t advanceX;    ///< Distance to advance cursor (x axis), 12.4 fixed-point in pixels
  int16_t left;         ///< X dist from cursor pos to UL corner
  int16_t top;          ///< Y dist from cursor pos to UL corner
  uint16_t dataLength;  ///< Size of the font data.
  uint32_t dataOffset;  ///< Pointer into EpdFont->bitmap (or within-group offset for compressed fonts)
} EpdGlyph;

/// Compressed font group: a DEFLATE-compressed block of glyph bitmaps
typedef struct {
  uint32_t compressedOffset;  ///< Byte offset into compressed data array
  uint32_t compressedSize;    ///< Compressed DEFLATE stream size
  uint32_t uncompressedSize;  ///< Decompressed size
  uint16_t glyphCount;        ///< Number of glyphs in this group
  uint32_t firstGlyphIndex;   ///< First glyph index in the global glyph array
} EpdFontGroup;

/// Glyph interval structure
typedef struct {
  uint32_t first;   ///< The first unicode code point of the interval
  uint32_t last;    ///< The last unicode code point of the interval
  uint32_t offset;  ///< Index of the first code point into the glyph array
} EpdUnicodeInterval;

/// Maps a codepoint to a kerning class ID, sorted by codepoint for binary search.
/// Class IDs are 1-based; codepoints not in the table have implicit class 0 (no kerning).
typedef struct {
  uint16_t codepoint;  ///< Unicode codepoint
  uint8_t classId;     ///< 1-based kerning class ID
} __attribute__((packed)) EpdKernClassEntry;

/// Ligature substitution for a specific glyph pair, sorted by `pair` for binary search.
/// `pair` encodes (leftCodepoint << 16 | rightCodepoint) for single-key lookup.
typedef struct {
  uint32_t pair;        ///< Packed codepoint pair (left << 16 | right)
  uint32_t ligatureCp;  ///< Codepoint of the replacement ligature glyph
} __attribute__((packed)) EpdLigaturePair;

/// Data stored for FONT AS A WHOLE
typedef struct {
  const uint8_t* bitmap;                ///< Glyph bitmaps, concatenated
  const EpdGlyph* glyph;                ///< Glyph array
  const EpdUnicodeInterval* intervals;  ///< Valid unicode intervals for this font
  uint32_t intervalCount;               ///< Number of unicode intervals.
  uint8_t advanceY;                     ///< Newline distance (y axis)
  int ascender;                         ///< Maximal height of a glyph above the base line
  int descender;                        ///< Maximal height of a glyph below the base line
  bool is2Bit;
  const EpdFontGroup* groups;                 ///< NULL for uncompressed fonts
  uint16_t groupCount;                        ///< 0 for uncompressed fonts
  const uint16_t* glyphToGroup;               ///< Per-glyph group ID (nullptr for contiguous-group fonts)
  const EpdKernClassEntry* kernLeftClasses;   ///< Sorted left-side class map (nullptr if none)
  const EpdKernClassEntry* kernRightClasses;  ///< Sorted right-side class map (nullptr if none)
  const int8_t* kernMatrix;              ///< Flat leftClassCount x rightClassCount matrix, 4.4 fixed-point in pixels
  uint16_t kernLeftEntryCount;           ///< Entries in kernLeftClasses
  uint16_t kernRightEntryCount;          ///< Entries in kernRightClasses
  uint8_t kernLeftClassCount;            ///< Number of distinct left classes (matrix rows)
  uint8_t kernRightClassCount;           ///< Number of distinct right classes (matrix cols)
  const EpdLigaturePair* ligaturePairs;  ///< Sorted ligature pair table (nullptr if none)
  uint32_t ligaturePairCount;            ///< Number of entries in ligaturePairs

  /// On-demand glyph loading for fonts that don't keep all glyphs in RAM (e.g. SD card fonts).
  /// Called by getGlyph() when a codepoint is not found in the interval table.
  /// Returns a valid EpdGlyph* with correct metadata, or nullptr to fall back to the
  /// replacement glyph.  The returned pointer is valid until the next glyphMissHandler
  /// call that causes a ring-buffer eviction — callers must consume it (measure or draw)
  /// before requesting another missed glyph.
  const EpdGlyph* (*glyphMissHandler)(void* ctx, uint32_t codepoint);

  /// Context pointer for glyphMissHandler (typically SdCardFont*).  Also used by
  /// GfxRenderer::getGlyphBitmap() to retrieve overflow bitmaps via SdCardFont.
  void* glyphMissCtx;
} EpdFontData;
