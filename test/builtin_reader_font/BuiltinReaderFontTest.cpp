#include <gtest/gtest.h>

#include "EpdFont.h"
#include "builtinFonts/notosans_12_bold.h"
#include "builtinFonts/notosans_12_bolditalic.h"
#include "builtinFonts/notosans_12_italic.h"
#include "builtinFonts/notosans_12_regular.h"
#include "builtinFonts/notosans_14_bold.h"
#include "builtinFonts/notosans_14_bolditalic.h"
#include "builtinFonts/notosans_14_italic.h"
#include "builtinFonts/notosans_14_regular.h"
#include "builtinFonts/notosans_16_bold.h"
#include "builtinFonts/notosans_16_bolditalic.h"
#include "builtinFonts/notosans_16_italic.h"
#include "builtinFonts/notosans_16_regular.h"
#include "builtinFonts/notosans_18_bold.h"
#include "builtinFonts/notosans_18_bolditalic.h"
#include "builtinFonts/notosans_18_italic.h"
#include "builtinFonts/notosans_18_regular.h"

namespace {

struct ReaderFont {
  const char* name;
  const EpdFontData* data;
};

constexpr ReaderFont kReaderFonts[] = {
    {"12 regular", &notosans_12_regular},     {"12 italic", &notosans_12_italic},
    {"12 bold", &notosans_12_bold},           {"12 bold italic", &notosans_12_bolditalic},
    {"14 regular", &notosans_14_regular},     {"14 italic", &notosans_14_italic},
    {"14 bold", &notosans_14_bold},           {"14 bold italic", &notosans_14_bolditalic},
    {"16 regular", &notosans_16_regular},     {"16 italic", &notosans_16_italic},
    {"16 bold", &notosans_16_bold},           {"16 bold italic", &notosans_16_bolditalic},
    {"18 regular", &notosans_18_regular},     {"18 italic", &notosans_18_italic},
    {"18 bold", &notosans_18_bold},           {"18 bold italic", &notosans_18_bolditalic},
};

}  // namespace

TEST(BuiltinReaderFont, EverySizeAndStyleIncludesThaiGlyphs) {
  for (const ReaderFont& readerFont : kReaderFonts) {
    SCOPED_TRACE(readerFont.name);
    const EpdFont font(readerFont.data);
    for (uint32_t cp = 0x0E01; cp <= 0x0E3A; ++cp) EXPECT_TRUE(font.hasCodepoint(cp)) << "missing U+" << cp;
    EXPECT_TRUE(font.hasCodepoint(0x0E3F)) << "missing Thai baht sign";
    for (uint32_t cp = 0x0E40; cp <= 0x0E5B; ++cp) EXPECT_TRUE(font.hasCodepoint(cp)) << "missing U+" << cp;
  }
}
