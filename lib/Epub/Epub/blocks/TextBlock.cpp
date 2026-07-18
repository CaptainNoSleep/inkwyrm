#include "TextBlock.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>

#include "../TypographyUtils.h"

namespace {
constexpr size_t TYPOGRAPHY_PREFIX_BUFFER = 8;

size_t firstUtf8CodepointBytes(const std::string& word) {
  if (word.empty()) return 0;
  const auto* begin = reinterpret_cast<const unsigned char*>(word.c_str());
  const auto* ptr = begin;
  utf8NextCodepoint(&ptr);
  return static_cast<size_t>(ptr - begin);
}
}  // namespace

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  // Focus annotations are optional: empty vectors mean no word in this block has a split.
  // When present, they must be sized in lockstep with words[].
  const bool hasFocus = !wordFocusBoundary.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (hasFocus && (words.size() != wordFocusBoundary.size() || words.size() != wordFocusSuffixX.size()))) {
    LOG_ERR("TXB", "Render skipped: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)\n",
            (uint32_t)words.size(), (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size(),
            (uint32_t)wordFocusBoundary.size(), (uint32_t)wordFocusSuffixX.size());
    return;
  }

  const bool scanning = renderer.isFontCacheScanning();
  const int ascender = renderer.getFontAscenderSize(fontId);
  for (size_t i = 0; i < words.size(); i++) {
    const int wordX = wordXpos[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyles[i];
    const int typographyFontId = TypographyUtils::resolveTypographyFontId(fontId, currentStyle);
    const auto baseDir = static_cast<BidiUtils::BidiBaseDir>(
        BidiUtils::detectParagraphLevel(words[i].c_str(), blockStyle.isRtl ? 1 : 0));
    const uint8_t boundary = hasFocus ? wordFocusBoundary[i] : 0;

    // SUP/SUB shift the baseline passed to drawText; the glyph is also scaled 50% inside
    // drawText, so these offsets are chosen relative to the full-size ascender:
    //   SUP: raise by 40% of ascender — sits clearly above the cap-height
    //   SUB: lower by 25% of ascender — descends below baseline without clashing with ascenders below
    int wordY = y;
    if ((currentStyle & EpdFontFamily::SUP) != 0) {
      wordY -= ascender * 2 / 5;
    } else if ((currentStyle & EpdFontFamily::SUB) != 0) {
      wordY += ascender / 4;
    }

    if (boundary > 0) {
      // Focus split: draw bold prefix, then the regular suffix at a pre-computed x offset.
      // The bold prefix is bounded to 9 codepoints by the clamp on targetBoldChars in
      // ParsedText::addWord; 9 UTF-8 codepoints occupy at most 9 * 4 = 36 bytes, +1 for null = 37.
      // suffixX is computed at cache-creation time to avoid font metric lookups at render time.
      static constexpr size_t MAX_FOCUS_PREFIX_BYTES = 9 * 4 + 1;
      char boldBuf[40];
      static_assert(sizeof(boldBuf) >= MAX_FOCUS_PREFIX_BYTES,
                    "boldBuf too small for max focus prefix (9 codepoints * 4 UTF-8 bytes + null)");
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      const size_t boldLen = std::min<size_t>({static_cast<size_t>(boundary), words[i].size(), sizeof(boldBuf) - 1});
      memcpy(boldBuf, words[i].c_str(), boldLen);
      boldBuf[boldLen] = '\0';
      renderer.drawText(typographyFontId, wordX, wordY, boldBuf, true, boldStyle, baseDir);
      const int suffixX = wordX + wordFocusSuffixX[i];
      renderer.drawText(typographyFontId, suffixX, wordY, words[i].c_str() + boldLen, true, currentStyle, baseDir);
    } else {
      if ((currentStyle & EpdFontFamily::DROP_CAP) != 0) {
        const std::string& word = words[i];
        const size_t prefixBytes = firstUtf8CodepointBytes(word);
        if (prefixBytes > 0 && prefixBytes < word.size()) {
          char prefix[TYPOGRAPHY_PREFIX_BUFFER] = {};
          const size_t copied = std::min(prefixBytes, sizeof(prefix) - 1);
          memcpy(prefix, word.data(), copied);
          prefix[copied] = '\0';
          const auto remainderStyle = static_cast<EpdFontFamily::Style>(currentStyle & ~EpdFontFamily::DROP_CAP);
          const int remainderFontId = TypographyUtils::resolveTypographyFontId(fontId, remainderStyle);
          renderer.drawText(typographyFontId, wordX, wordY, prefix, true, currentStyle, baseDir);
          const int prefixAdvance = renderer.getTextAdvanceX(typographyFontId, prefix, currentStyle);
          renderer.drawText(remainderFontId, wordX + prefixAdvance, wordY, word.c_str() + prefixBytes, true,
                            remainderStyle, baseDir);
        } else {
          renderer.drawText(typographyFontId, wordX, wordY, word.c_str(), true, currentStyle, baseDir);
        }
      } else {
        renderer.drawText(typographyFontId, wordX, wordY, words[i].c_str(), true, currentStyle, baseDir);
      }
    }

    if (!scanning && (currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      const std::string& w = words[i];
      int underlineWidth = renderer.getTextWidth(typographyFontId, w.c_str(), currentStyle, baseDir);
      const int underlineY = wordY + ascender + 2;

      if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
        underlineWidth = (underlineWidth + 1) / 2;
      }

      renderer.drawLine(wordX, underlineY, wordX + underlineWidth, underlineY, true);
    }
  }
}

bool TextBlock::serialize(HalFile& file) const {
  // Focus annotations are optional; vectors are either empty (no splits in this block)
  // or sized in lockstep with words[].
  const bool hasFocus = !wordFocusBoundary.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() ||
      (hasFocus && (words.size() != wordFocusBoundary.size() || words.size() != wordFocusSuffixX.size()))) {
    LOG_ERR("TXB", "Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)\n",
            static_cast<uint32_t>(words.size()), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(wordFocusBoundary.size()),
            static_cast<uint32_t>(wordFocusSuffixX.size()));
    return false;
  }

  // Word data
  serialization::writePod(file, static_cast<uint16_t>(words.size()));
  for (const auto& w : words) serialization::writeString(file, w);
  for (auto x : wordXpos) serialization::writePod(file, x);
  for (auto s : wordStyles) serialization::writePod(file, s);
  // Focus block: 1-byte presence flag, followed by per-word vectors only when present.
  // Saves 3 bytes/word when focus reading is disabled or no word on this line was split.
  serialization::writePod(file, static_cast<uint8_t>(hasFocus ? 1 : 0));
  if (hasFocus) {
    for (auto b : wordFocusBoundary) serialization::writePod(file, b);
    for (auto sx : wordFocusSuffixX) serialization::writePod(file, sx);
  }

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.lineHeight.value);
  serialization::writePod(file, blockStyle.lineHeight.unit);
  serialization::writePod(file, blockStyle.lineHeightDefined);
  serialization::writePod(file, blockStyle.lineHeightIsMultiplier);
  serialization::writePod(file, blockStyle.fontVariantSmallCaps);
  serialization::writePod(file, blockStyle.initialLetter);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);
  serialization::writePod(file, blockStyle.isRtl);
  serialization::writePod(file, blockStyle.directionDefined);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(HalFile& file) {
  uint16_t wc;
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<uint8_t> wordFocusBoundary;
  std::vector<uint16_t> wordFocusSuffixX;
  BlockStyle blockStyle;

  // Word count
  serialization::readPod(file, wc);

  // Sanity check: prevent allocation of unreasonably large vectors (max 10000 words per block)
  if (wc > 10000) {
    LOG_ERR("TXB", "Deserialization failed: word count %u exceeds maximum", wc);
    return nullptr;
  }

  // Word data
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  for (auto& w : words) serialization::readString(file, w);
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);
  // Focus block: presence flag, then vectors only if present. Empty vectors when absent
  // signal "no splits in this block" to render() (zero per-word RAM cost).
  uint8_t hasFocus;
  serialization::readPod(file, hasFocus);
  if (hasFocus) {
    wordFocusBoundary.resize(wc);
    wordFocusSuffixX.resize(wc);
    for (auto& b : wordFocusBoundary) serialization::readPod(file, b);
    for (auto& sx : wordFocusSuffixX) serialization::readPod(file, sx);
  }

  // Style (alignment + margins/padding/indent)
  serialization::readPod(file, blockStyle.alignment);
  serialization::readPod(file, blockStyle.textAlignDefined);
  serialization::readPod(file, blockStyle.lineHeight.value);
  serialization::readPod(file, blockStyle.lineHeight.unit);
  serialization::readPod(file, blockStyle.lineHeightDefined);
  serialization::readPod(file, blockStyle.lineHeightIsMultiplier);
  serialization::readPod(file, blockStyle.fontVariantSmallCaps);
  serialization::readPod(file, blockStyle.initialLetter);
  serialization::readPod(file, blockStyle.marginTop);
  serialization::readPod(file, blockStyle.marginBottom);
  serialization::readPod(file, blockStyle.marginLeft);
  serialization::readPod(file, blockStyle.marginRight);
  serialization::readPod(file, blockStyle.paddingTop);
  serialization::readPod(file, blockStyle.paddingBottom);
  serialization::readPod(file, blockStyle.paddingLeft);
  serialization::readPod(file, blockStyle.paddingRight);
  serialization::readPod(file, blockStyle.textIndent);
  serialization::readPod(file, blockStyle.textIndentDefined);
  serialization::readPod(file, blockStyle.isRtl);
  serialization::readPod(file, blockStyle.directionDefined);

  return std::unique_ptr<TextBlock>(new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles),
                                                  std::move(wordFocusBoundary), std::move(wordFocusSuffixX),
                                                  blockStyle));
}
