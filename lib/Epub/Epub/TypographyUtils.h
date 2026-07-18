#pragma once

#include <algorithm>
#include <string>

#include "../../EpdFont/EpdFontFamily.h"
#include "../../../src/fontIds.h"

namespace TypographyUtils {

inline void toUpperAsciiInPlace(std::string& text) {
  for (char& ch : text) {
    if (ch >= 'a' && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }
}

// 1.4.1 base ships two built-in reader families (NOTOSERIF is the default reader
// font). Master's BOOKERLY/LEXEND/OPENDYSLEXIC ladders don't exist here; sizes
// clamp at the 12/18 rails. Extend these ladders when the SD-font offload lands.
inline int smallerFontId(const int fontId) {
  switch (fontId) {
    case NOTOSERIF_18_FONT_ID:
      return NOTOSERIF_16_FONT_ID;
    case NOTOSERIF_16_FONT_ID:
      return NOTOSERIF_14_FONT_ID;
    case NOTOSERIF_14_FONT_ID:
      return NOTOSERIF_12_FONT_ID;
    case NOTOSERIF_12_FONT_ID:
      return NOTOSERIF_12_FONT_ID;
    case NOTOSANS_18_FONT_ID:
      return NOTOSANS_16_FONT_ID;
    case NOTOSANS_16_FONT_ID:
      return NOTOSANS_14_FONT_ID;
    case NOTOSANS_14_FONT_ID:
      return NOTOSANS_12_FONT_ID;
    case NOTOSANS_12_FONT_ID:
      return NOTOSANS_12_FONT_ID;
    default:
      return fontId;
  }
}

inline int largerFontId(const int fontId) {
  switch (fontId) {
    case NOTOSERIF_12_FONT_ID:
      return NOTOSERIF_14_FONT_ID;
    case NOTOSERIF_14_FONT_ID:
      return NOTOSERIF_16_FONT_ID;
    case NOTOSERIF_16_FONT_ID:
      return NOTOSERIF_18_FONT_ID;
    case NOTOSERIF_18_FONT_ID:
      return NOTOSERIF_18_FONT_ID;
    case NOTOSANS_12_FONT_ID:
      return NOTOSANS_14_FONT_ID;
    case NOTOSANS_14_FONT_ID:
      return NOTOSANS_16_FONT_ID;
    case NOTOSANS_16_FONT_ID:
      return NOTOSANS_18_FONT_ID;
    case NOTOSANS_18_FONT_ID:
      return NOTOSANS_18_FONT_ID;
    default:
      return fontId;
  }
}

inline int resolveTypographyFontId(const int fontId, const EpdFontFamily::Style style) {
  if ((style & EpdFontFamily::DROP_CAP) != 0) {
    return largerFontId(fontId);
  }
  if ((style & EpdFontFamily::SMALL_CAPS) != 0) {
    return smallerFontId(fontId);
  }
  return fontId;
}

}  // namespace TypographyUtils
