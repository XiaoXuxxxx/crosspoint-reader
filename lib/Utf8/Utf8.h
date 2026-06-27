#pragma once

#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

uint32_t utf8NextCodepoint(const unsigned char** string);
// Appends a Unicode codepoint to a std::string in UTF-8 encoding.
void utf8AppendCodepoint(uint32_t cp, std::string& out);
// Remove the last UTF-8 codepoint from a std::string and return the new size.
size_t utf8RemoveLastChar(std::string& str);
// Truncate string by removing N UTF-8 codepoints from the end.
void utf8TruncateChars(std::string& str, size_t numChars);

// Canonical composition (NFC) for the Latin / Vietnamese range: precomposes a
// base letter followed by combining diacritical mark(s) into a single codepoint.
// Needed because the device fonts have no combining-mark positioning, so text
// stored in NFD (e.g. some EPUB chapter titles) otherwise renders broken.
std::string utf8ComposeNfc(const std::string& in);

// Truncate a raw char buffer to the last complete UTF-8 codepoint boundary.
// Returns the new length (<= len). If the buffer ends mid-sequence, the
// incomplete trailing bytes are excluded.
int utf8SafeTruncateBuffer(const char* buf, int len);

// Returns true for CJK characters that allow line breaks on either side without hyphenation.
// Covers CJK Unified Ideographs, Hiragana, Katakana, Hangul Syllables, CJK punctuation,
// and fullwidth forms — the ranges where word boundaries are implicit per character.
inline bool utf8IsCjkBreakable(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)        // Hangul Jamo
         || (cp >= 0x3000 && cp <= 0x303F)     // CJK Symbols and Punctuation
         || (cp >= 0x3040 && cp <= 0x309F)     // Hiragana
         || (cp >= 0x30A0 && cp <= 0x30FF)     // Katakana
         || (cp >= 0x3130 && cp <= 0x318F)     // Hangul Compatibility Jamo
         || (cp >= 0x3400 && cp <= 0x4DBF)     // CJK Extension A
         || (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK Unified Ideographs
         || (cp >= 0xAC00 && cp <= 0xD7AF)     // Hangul Syllables
         || (cp >= 0xD7B0 && cp <= 0xD7FF)     // Hangul Jamo Extended-B
         || (cp >= 0xF900 && cp <= 0xFAFF)     // CJK Compatibility Ideographs
         || (cp >= 0xFE30 && cp <= 0xFE4F)     // CJK Compatibility Forms
         || (cp >= 0xFF01 && cp <= 0xFF60)     // Fullwidth Latin / Punctuation
         || (cp >= 0xFF65 && cp <= 0xFFEF)     // Halfwidth Katakana / Hangul
         || (cp >= 0x20000 && cp <= 0x2A6DF)   // CJK Extension B
         || (cp >= 0x2A700 && cp <= 0x2B73F);  // CJK Extension C
}

// Returns true for Unicode combining diacritical marks that should not advance the cursor.
inline bool utf8IsCombiningMark(const uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F)      // Combining Diacritical Marks
         || (cp >= 0x1DC0 && cp <= 0x1DFF)   // Combining Diacritical Marks Supplement
         || (cp >= 0x20D0 && cp <= 0x20FF)   // Combining Diacritical Marks for Symbols
         || (cp >= 0xFE20 && cp <= 0xFE2F)   // Combining Half Marks
         || (cp == 0x0E31)                   // Thai Mai Han-Akat (above-vowel)
         || (cp >= 0x0E34 && cp <= 0x0E3A)   // Thai above-vowels (0E34-0E37) + below-vowels (0E38-0E3A)
         || (cp >= 0x0E47 && cp <= 0x0E4E);  // Thai Mai Tai Khue + tone marks + Nikhahit + Mai Yamok
}

// Returns true for Thai combining marks (above-vowels, below-vowels, tone marks, etc.).
// Used to apply Thai-specific mark positioning (post-base pen position) instead of
// the generic centerOver() approach.
inline bool utf8IsThaiCombiningMark(const uint32_t cp) {
  return (cp == 0x0E31)                      // Thai Mai Han-Akat (above)
         || (cp >= 0x0E34 && cp <= 0x0E3A)   // Thai vowels above (0E34-0E37) + below (0E38-0E3A)
         || (cp >= 0x0E47 && cp <= 0x0E4E);  // Thai Mai Tai Khue + tone marks + Nikhahit + above-top
}

// Thai combining mark vertical level, aligned with libthai (thctype.c th_chlevel).
// ABOVE1: above-vowels closest to base (libthai level 1)
// ABOVE2: hilo-or-top marks — Mai Tai Khue, Nikhahit (libthai level 3)
// ABOVE3: tone marks + thanthakhat (libthai level 2 / top)
// BELOW1: below-vowels closest to base (libthai level -1)
// BELOW2: Phinthu, lowest (libthai level -1)
enum class ThaiMarkLevel : uint8_t {
  None = 0,  // Not a Thai mark (Latin/Hebrew mark or base character)
  Above1,    // U+0E31, U+0E34-0E37, U+0E4E (vowels + Mai Yamok above, closest to base)
  Above2,    // U+0E47, U+0E4D (Mai Tai Khue, Nikhahit — hilo-or-top)
  Above3,    // U+0E48-0E4C (tone marks + thanthakhat — top)
  Below1,    // U+0E38-0E39 (vowels below, closest)
  Below2,    // U+0E3A (Phinthu, lowest)
};

// Returns the Thai mark vertical level, or None if cp is not a Thai combining mark.
// Classification follows libthai th_chlevel: U+0E4E is level 1 (with vowels),
// U+0E4C is level 2 (with tone marks), U+0E47/U+0E4D are level 3 (hilo-or-top).
inline ThaiMarkLevel thaiMarkLevel(const uint32_t cp) {
  if (cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E37) || cp == 0x0E4E) return ThaiMarkLevel::Above1;
  if (cp == 0x0E47 || cp == 0x0E4D) return ThaiMarkLevel::Above2;
  if (cp >= 0x0E48 && cp <= 0x0E4C) return ThaiMarkLevel::Above3;
  if (cp >= 0x0E38 && cp <= 0x0E39) return ThaiMarkLevel::Below1;
  if (cp == 0x0E3A) return ThaiMarkLevel::Below2;
  return ThaiMarkLevel::None;
}

// Decompose U+0E33 (Sara Am) → U+0E4D (Nikhahit) + U+0E32 (Sara Aa) with tone reorder.
// If tone marks (U+0E48-0E4B) precede the Sara Am, Nikhahit is inserted before them:
//   <C, tone, 0E33> → <C, 0E4D, tone, 0E32>
// This matches the gen_thai_dict.py decompose_sara_am() rule exactly.
std::string utf8DecomposeThaiSaraAm(const std::string& in);
