#pragma once

#include <Epub/Page.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace HighlightGeometry {

struct WordInfo {
  std::string text;
  int16_t screenX = 0;
  int16_t screenY = 0;
  int16_t width = 0;
  int16_t row = 0;
};

struct RowInfo {
  int16_t y = 0;
  std::vector<int> wordIndices;
};

inline int measureWordWidth(const GfxRenderer& renderer, const int readerFontId, const char* text) {
  return renderer.getTextAdvanceX(readerFontId, text, EpdFontFamily::REGULAR);
}

inline void prewarmReaderFont(const Page& page, GfxRenderer& renderer, const int readerFontId) {
  if (!renderer.isSdCardFont(readerFontId)) {
    return;
  }

  std::string pageText;
  pageText.reserve(2048);
  for (const auto& element : page.elements) {
    if (!element || element->getTag() != TAG_PageLine) {
      continue;
    }
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) {
      continue;
    }
    for (const auto& word : block->getWords()) {
      if (!pageText.empty()) {
        pageText.push_back(' ');
      }
      pageText += word;
    }
  }

  if (!pageText.empty()) {
    renderer.ensureSdCardFontReady(readerFontId, pageText.c_str(), 0x01);
  }
}

inline void extractWords(const Page& page, GfxRenderer& renderer, const int readerFontId,
                         const int marginLeft, const int marginTop, std::vector<WordInfo>& words,
                         std::vector<RowInfo>& rows) {
  words.clear();
  rows.clear();
  prewarmReaderFont(page, renderer, readerFontId);

  for (const auto& element : page.elements) {
    if (!element || element->getTag() != TAG_PageLine) {
      continue;
    }
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) {
      continue;
    }

    const auto& wordList = block->getWords();
    const auto& xPositions = block->getWordXpos();
    const size_t count = std::min(wordList.size(), xPositions.size());
    for (size_t i = 0; i < count; ++i) {
      if (wordList[i].empty()) {
        continue;
      }
      const int16_t x = static_cast<int16_t>(line.xPos + xPositions[i] + marginLeft);
      const int16_t y = static_cast<int16_t>(line.yPos + marginTop);
      const int16_t width = static_cast<int16_t>(std::max(1, measureWordWidth(renderer, readerFontId, wordList[i].c_str())));
      words.push_back(WordInfo{wordList[i], x, y, width, 0});
    }
  }

  if (words.empty()) {
    return;
  }

  std::sort(words.begin(), words.end(), [](const WordInfo& a, const WordInfo& b) {
    // Strict weak ordering (lexicographic). The +/-2px same-row tolerance must NOT
    // live in the comparator (non-transitive => std::sort UB); the row-grouping
    // pass below applies it linearly after the sort.
    if (a.screenY != b.screenY) {
      return a.screenY < b.screenY;
    }
    return a.screenX < b.screenX;
  });

  int16_t currentY = words[0].screenY;
  rows.push_back(RowInfo{currentY, {}});
  for (size_t i = 0; i < words.size(); ++i) {
    if (std::abs(words[i].screenY - currentY) > 2) {
      currentY = words[i].screenY;
      rows.push_back(RowInfo{currentY, {}});
    }
    words[i].row = static_cast<int16_t>(rows.size() - 1);
    rows.back().wordIndices.push_back(static_cast<int>(i));
  }
}

inline bool findWordCursorForRow(const std::vector<RowInfo>& rows, const int rowIndex, const int wordIndexInRow,
                                int& outWordIndex) {
  if (rowIndex < 0 || rowIndex >= static_cast<int>(rows.size())) {
    return false;
  }
  const auto& row = rows[static_cast<size_t>(rowIndex)];
  if (wordIndexInRow < 0 || wordIndexInRow >= static_cast<int>(row.wordIndices.size())) {
    return false;
  }
  outWordIndex = row.wordIndices[static_cast<size_t>(wordIndexInRow)];
  return true;
}

}  // namespace HighlightGeometry
