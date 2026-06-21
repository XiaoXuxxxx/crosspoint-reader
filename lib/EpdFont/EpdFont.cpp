#include "EpdFont.h"

#include <Utf8.h>

#include <algorithm>

void EpdFont::getTextBounds(const char* string, const int startX, const int startY, int* minX, int* minY, int* maxX,
                            int* maxY) const {
  *minX = startX;
  *minY = startY;
  *maxX = startX;
  *maxY = startY;

  if (*string == '\0') {
    return;
  }

  int lastBaseX = startX;
  int lastBaseLeft = 0;
  int lastBaseWidth = 0;
  int lastBaseTop = 0;
  int lastBaseHeight = 0;
  int lastMarkTop = 0;
  uint32_t lastBaseCp = 0;
  // Thai cell-slot occupancy (libthai thcell.c model)
  bool hiloOccupied = false;
  bool topOccupied = false;
  bool belowOccupied = false;
  int32_t prevAdvanceFP = 0;  // 12.4 fixed-point: prev glyph's advance + next kern for snap
  uint32_t cp;
  uint32_t prevCp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&string)))) {
    const bool isCombining = utf8IsCombiningMark(cp);

    if (!isCombining) {
      cp = applyLigatures(cp, string);
    }

    const EpdGlyph* glyph = getGlyph(cp);
    if (!glyph) {
      // Keep cursor movement stable when a base glyph is missing, but don't attach subsequent
      // combining marks to stale base metrics.
      if (!isCombining) {
        lastBaseX += fp4::toPixel(prevAdvanceFP);  // flush pending advance before resetting
        prevCp = 0;
        prevAdvanceFP = 0;
        lastBaseLeft = 0;
        lastBaseWidth = 0;
        lastBaseTop = 0;
        lastBaseHeight = 0;
        lastBaseCp = 0;
        lastMarkTop = 0;
        hiloOccupied = false;
        topOccupied = false;
        belowOccupied = false;
      }
      continue;
    }

    const ThaiMarkLevel level = thaiMarkLevel(cp);
    const bool isThaiBelowMark = (level == ThaiMarkLevel::Below1 || level == ThaiMarkLevel::Below2);

    // Thai composibility checks (WTT table, libthai thcell.c):
    // - Reject mark if no valid base
    // - Reject second above-vowel (AV1+AV2)
    // - Reject consecutive tone marks
    // - Reject second below-vowel
    // - Level 3 marks (็ U+0E47, ญ U+0E4D): route to hilo if empty, else top
    if (isCombining && level != ThaiMarkLevel::None) {
      if (lastBaseCp == 0) continue;
      if (isThaiBelowMark) {
        if (belowOccupied) continue;
        belowOccupied = true;
      } else if (level == ThaiMarkLevel::Above2) {
        // Level 3 hilo-or-top routing
        if (!hiloOccupied) {
          hiloOccupied = true;
        } else {
          topOccupied = true;
        }
      } else if (level == ThaiMarkLevel::Above1) {
        if (hiloOccupied) continue;
        hiloOccupied = true;
      } else if (level == ThaiMarkLevel::Above3) {
        if (topOccupied) continue;
        topOccupied = true;
      }
    }

    // Aligned with libthai: above-marks use level=1 gap; stacking via lastMarkTop
    // handles vertical tiering (tone on vowel = higher, tone on base = lower).
    int adjustY = 0;
    if (isCombining) {
      if (isThaiBelowMark) {
        adjustY = combiningMark::lowerBelowBase(glyph->top, lastBaseTop - lastBaseHeight,
                                                static_cast<int>(level) - static_cast<int>(ThaiMarkLevel::Below1) + 1);
        // Extra lowering for descender and undersplit (tail-cut approximation)
        const ThaiConsonantClass cc = thaiConsonantClass(lastBaseCp);
        if (cc == ThaiConsonantClass::Descender) {
          adjustY += glyph->height / 2;
        } else if (cc == ThaiConsonantClass::Undersplit) {
          adjustY += glyph->height;
        }
      } else {
        adjustY =
            combiningMark::raiseAboveBase(glyph->top, glyph->height, (lastMarkTop != 0) ? lastMarkTop : lastBaseTop, 1);
      }
    }

    if (!isCombining && prevCp != 0) {
      const auto kernFP = getKerning(prevCp, cp);  // 4.4 fixed-point kern
      lastBaseX += fp4::toPixel(prevAdvanceFP + kernFP);
    }

    const int glyphBaseX =
        isCombining ? getCombiningAnchorX(fp4::fromPixel(lastBaseX) + prevAdvanceFP, lastBaseX, prevAdvanceFP, cp)
                    : lastBaseX;
    const int glyphBaseY =
        startY - (isCombining && !isThaiBelowMark ? adjustY : 0) + (isCombining && isThaiBelowMark ? adjustY : 0);

    *minX = std::min(*minX, glyphBaseX + glyph->left);
    *maxX = std::max(*maxX, glyphBaseX + glyph->left + glyph->width);
    *minY = std::min(*minY, glyphBaseY + glyph->top - glyph->height);
    *maxY = std::max(*maxY, glyphBaseY + glyph->top);

    if (isCombining && !isThaiBelowMark) {
      lastMarkTop = glyph->top - adjustY;
    }

    if (!isCombining) {
      lastBaseLeft = glyph->left;
      lastBaseWidth = glyph->width;
      lastBaseTop = glyph->top;
      lastBaseHeight = glyph->height;
      lastBaseCp = cp;
      lastMarkTop = 0;
      hiloOccupied = false;
      topOccupied = false;
      belowOccupied = false;
      prevAdvanceFP = glyph->advanceX;  // 12.4 fixed-point
      prevCp = cp;
    }
  }
}

void EpdFont::getTextDimensions(const char* string, int* w, int* h) const {
  int minX = 0, minY = 0, maxX = 0, maxY = 0;

  getTextBounds(string, 0, 0, &minX, &minY, &maxX, &maxY);

  *w = maxX - minX;
  *h = maxY - minY;
}

static uint8_t lookupKernClass(const EpdKernClassEntry* entries, const uint16_t count, const uint32_t cp) {
  if (!entries || count == 0 || cp > 0xFFFF) {
    return 0;
  }

  const auto target = static_cast<uint16_t>(cp);
  const auto* end = entries + count;

  // lower_bound: exact-key lookup. Finds the first entry with codepoint >= target,
  // then the equality check confirms an exact match exists.
  const auto it = std::lower_bound(
      entries, end, target, [](const EpdKernClassEntry& entry, uint16_t value) { return entry.codepoint < value; });

  if (it != end && it->codepoint == target) {
    return it->classId;
  }

  return 0;
}

int8_t EpdFont::getKerning(const uint32_t leftCp, const uint32_t rightCp) const {
  if (utf8IsCjkBreakable(leftCp) || utf8IsCjkBreakable(rightCp)) {
    return 0;
  }
  if (!data->kernMatrix) {
    return 0;
  }
  const uint8_t lc = lookupKernClass(data->kernLeftClasses, data->kernLeftEntryCount, leftCp);
  if (lc == 0) return 0;
  const uint8_t rc = lookupKernClass(data->kernRightClasses, data->kernRightEntryCount, rightCp);
  if (rc == 0) return 0;
  return data->kernMatrix[(lc - 1) * data->kernRightClassCount + (rc - 1)];
}

uint32_t EpdFont::getLigature(const uint32_t leftCp, const uint32_t rightCp) const {
  const auto* pairs = data->ligaturePairs;
  const auto count = data->ligaturePairCount;
  if (!pairs || count == 0 || leftCp > 0xFFFF || rightCp > 0xFFFF) {
    return 0;
  }

  const uint32_t key = (leftCp << 16) | rightCp;
  const auto* end = pairs + count;

  // lower_bound: exact-key lookup. Finds the first entry with pair >= key,
  // then the equality check confirms an exact match exists.
  const auto it =
      std::lower_bound(pairs, end, key, [](const EpdLigaturePair& pair, uint32_t value) { return pair.pair < value; });

  if (it != end && it->pair == key) {
    return it->ligatureCp;
  }

  return 0;
}

uint32_t EpdFont::applyLigatures(uint32_t cp, const char*& text) const {
  if (!data->ligaturePairs || data->ligaturePairCount == 0) {
    return cp;
  }
  while (true) {
    const auto saved = reinterpret_cast<const uint8_t*>(text);
    const uint32_t nextCp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text));
    if (nextCp == 0) break;
    const uint32_t lig = getLigature(cp, nextCp);
    if (lig == 0) {
      text = reinterpret_cast<const char*>(saved);
      break;
    }
    cp = lig;
  }
  return cp;
}

const EpdGlyph* EpdFont::getGlyph(const uint32_t cp) const {
  const int count = data->intervalCount;
  if (count == 0 && !data->glyphMissHandler) return nullptr;

  if (count > 0) {
    const EpdUnicodeInterval* intervals = data->intervals;
    const auto* end = intervals + count;

    // upper_bound: range lookup. Finds the first interval with first > cp, so the
    // interval just before it is the last one with first <= cp. That's the only
    // candidate that could contain cp. Then we verify cp <= candidate.last.
    const auto it = std::upper_bound(
        intervals, end, cp, [](uint32_t value, const EpdUnicodeInterval& interval) { return value < interval.first; });

    if (it != intervals) {
      const auto& interval = *(it - 1);
      if (cp <= interval.last) {
        return &data->glyph[interval.offset + (cp - interval.first)];
      }
    }
  }

  // Codepoint not in interval table — try on-demand loading (SD card fonts).
  if (data->glyphMissHandler) {
    const EpdGlyph* loaded = data->glyphMissHandler(data->glyphMissCtx, cp);
    if (loaded) return loaded;
  }

  if (cp != REPLACEMENT_GLYPH) {
    return getGlyph(REPLACEMENT_GLYPH);
  }
  return nullptr;
}
