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

// Canonical composition (NFC) for the Latin / Vietnamese range and Hangul:
// precomposes a base letter followed by combining diacritical mark(s), and
// conjoining Hangul jamo sequences (L+V[+T]), into single codepoints. Needed
// because the device fonts have no combining-mark positioning and carry only
// precomposed Hangul syllables, so NFD text (some EPUB chapter titles; every
// filename written by macOS) otherwise renders broken or blank.
// Report absorbed input codepoint positions when requested; return empty if the callback fails.
std::string utf8ComposeNfc(const std::string& in, bool (*onAbsorbed)(uint32_t, void*) = nullptr,
                           void* context = nullptr);

// The composition utf8ComposeNfc() applies to a pending base and the next
// codepoint (a combining mark or a trailing Hangul jamo), or 0 when the pair
// does not compose. Exposed for callers that must follow NFC codepoint by
// codepoint, such as source-offset mapping.
uint32_t utf8ComposePair(uint32_t base, uint32_t mark);

// Compose a null-terminated display buffer without allocating or growing it.
// Uncomposed bytes (including malformed UTF-8) are preserved unchanged.
void utf8ComposeNfcInPlace(char* buffer);

// The base letter a precomposed codepoint decomposes to, or 0 when there is
// none ("é" -> "e", but "ø" -> 0: it is a letter in its own right, not
// o-with-stroke). Lives here rather than in a caller because the compose table
// is a ~5 KB static array in the header: a second includer is a second copy in
// flash.
//
// A linear scan, unlike utf8ComposePair's binary search above it: the table is
// sorted by (base, mark), which this lookup searches against the grain. Adding
// a second table sorted by composed would cost more flash than sharing this one
// saves.
uint32_t utf8DecomposedBase(uint32_t cp);

// Truncate a raw char buffer to the last complete UTF-8 codepoint boundary.
// Returns the new length (<= len). If the buffer ends mid-sequence, the
// incomplete trailing bytes are excluded.
int utf8SafeTruncateBuffer(const char* buf, int len);

// Returns the byte offset of the lead byte of the last complete UTF-8 codepoint
// in buf[0, len). Returns 0 if len <= 0 or buf[0, len) holds only continuation
// bytes (malformed input).
size_t utf8LastCodepointStart(const char* buf, size_t len);

// Returns true for CJK characters that break without hyphenation. Covers CJK Unified Ideographs,
// Hiragana, Katakana, Hangul, CJK punctuation, and fullwidth forms. Han and Kana have implicit
// word boundaries per character; Hangul is space-delimited, so layout keeps Hangul words whole
// (see utf8IsHangul) and splits one without a hyphen only through the hyphenation path.
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

// Returns true for Hangul letters. Korean separates words with spaces, so a boundary touching
// Hangul is never an implicit line-break opportunity (CSS word-break: keep-all).
inline bool utf8IsHangul(const uint32_t cp) {
  return (cp >= 0x1100 && cp <= 0x11FF)      // Hangul Jamo
         || (cp >= 0x3130 && cp <= 0x318F)   // Hangul Compatibility Jamo
         || (cp >= 0xA960 && cp <= 0xA97F)   // Hangul Jamo Extended-A
         || (cp >= 0xAC00 && cp <= 0xD7FF)   // Hangul Syllables, Hangul Jamo Extended-B
         || (cp >= 0xFFA0 && cp <= 0xFFDC);  // Halfwidth Hangul
}

// Writes cp as UTF-8 (1-4 bytes, no terminator) and returns the position after it.
inline char* utf8AppendCodepoint(char* output, const uint32_t codepoint) {
  if (codepoint < 0x80) {
    *output++ = static_cast<char>(codepoint);
  } else if (codepoint < 0x800) {
    *output++ = static_cast<char>(0xC0 | (codepoint >> 6));
    *output++ = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x10000) {
    *output++ = static_cast<char>(0xE0 | (codepoint >> 12));
    *output++ = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    *output++ = static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    *output++ = static_cast<char>(0xF0 | (codepoint >> 18));
    *output++ = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    *output++ = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    *output++ = static_cast<char>(0x80 | (codepoint & 0x3F));
  }
  return output;
}

// Uppercase partner of a lowercase Latin, Greek or Cyrillic letter; other codepoints are
// returned unchanged. Used to synthesize small caps, so letters without a single-codepoint
// uppercase form (e.g. U+00DF) stay as they are.
inline uint32_t utf8SmallCapsUpper(const uint32_t cp) {
  if (cp >= 'a' && cp <= 'z') return cp - 0x20;
  if (cp < 0xE0) return cp;
  if (cp <= 0xFE) return cp == 0xF7 ? cp : cp - 0x20;  // Latin-1 (skips the division sign)
  if (cp == 0xFF) return 0x178;
  if (cp <= 0x17F) {
    // Latin Extended-A pairs: lowercase on odd codepoints, except the two odd-aligned runs.
    const bool oddAligned = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
    if (cp == 0x131 || cp == 0x138 || cp == 0x149 || cp == 0x17F) return cp;
    return (cp % 2 == (oddAligned ? 0u : 1u)) ? cp - 1 : cp;
  }
  if (cp >= 0x3B1 && cp <= 0x3C9) return cp == 0x3C2 ? 0x3A3 : cp - 0x20;  // Greek (final sigma -> Sigma)
  if (cp >= 0x430 && cp <= 0x44F) return cp - 0x20;                        // Cyrillic
  if (cp >= 0x450 && cp <= 0x45F) return cp - 0x50;
  return cp;
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
// Single source of truth for Thai mark codepoint ranges — utf8IsThaiCombiningMark()
// and utf8IsCombiningMark() below both derive from this instead of re-testing ranges.
constexpr ThaiMarkLevel thaiMarkLevel(const uint32_t cp) {
  if (cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E37) || cp == 0x0E4E) return ThaiMarkLevel::Above1;
  if (cp == 0x0E47 || cp == 0x0E4D) return ThaiMarkLevel::Above2;
  if (cp >= 0x0E48 && cp <= 0x0E4C) return ThaiMarkLevel::Above3;
  if (cp >= 0x0E38 && cp <= 0x0E39) return ThaiMarkLevel::Below1;
  if (cp == 0x0E3A) return ThaiMarkLevel::Below2;
  return ThaiMarkLevel::None;
}

// Returns true for Thai combining marks (above-vowels, below-vowels, tone marks, etc.).
// Used to apply Thai-specific mark positioning (post-base pen position) instead of
// the generic centerOver() approach.
constexpr bool utf8IsThaiCombiningMark(const uint32_t cp) { return thaiMarkLevel(cp) != ThaiMarkLevel::None; }

// Thai letters, vowels, marks, digits and signs (U+0E01-0E5B).
constexpr bool utf8IsThaiCodepoint(const uint32_t cp) { return cp >= 0x0E01 && cp <= 0x0E5B; }

// Thai leading vowels (U+0E40-0E44), written before the consonant they follow in speech.
constexpr bool utf8IsThaiLeadingVowel(const uint32_t cp) { return cp >= 0x0E40 && cp <= 0x0E44; }

// Thai spacing signs that close a syllable or word and so cannot start a line:
// Paiyannoi (U+0E2F), Sara A (U+0E30), Sara Aa (U+0E32), Sara Am (U+0E33),
// Lakkhangyao (U+0E45) and Mai Yamok (U+0E46).
constexpr bool utf8IsThaiNonStarter(const uint32_t cp) {
  return cp == 0x0E2F || cp == 0x0E30 || cp == 0x0E32 || cp == 0x0E33 || cp == 0x0E45 || cp == 0x0E46;
}

// Returns true for Unicode combining diacritical marks that should not advance the cursor.
inline bool utf8IsCombiningMark(const uint32_t cp) {
  if (cp < 0x0300) return false;            // ASCII and Latin-1: the common case
  return cp <= 0x036F                       // Combining Diacritical Marks
         || (cp >= 0x1DC0 && cp <= 0x1DFF)  // Combining Diacritical Marks Supplement
         || (cp >= 0x20D0 && cp <= 0x20FF)  // Combining Diacritical Marks for Symbols
         || (cp >= 0xFE20 && cp <= 0xFE2F)  // Combining Half Marks
         || utf8IsThaiCombiningMark(cp);
}

// Decompose U+0E33 (Sara Am) → U+0E4D (Nikhahit) + U+0E32 (Sara Aa) with tone reorder.
// If tone marks (U+0E48-0E4B) precede the Sara Am, Nikhahit is inserted before them:
//   <C, tone, 0E33> → <C, 0E4D, tone, 0E32>
// This matches the gen_thai_lexicon.py decompose_sara_am() rule exactly.
// Text without Sara Am is returned as is (moved, not copied).
std::string utf8DecomposeThaiSaraAm(std::string in);
