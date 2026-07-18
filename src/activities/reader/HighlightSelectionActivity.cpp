#include "HighlightSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <new>
#include <ctime>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int HIGHLIGHT_PADDING_X = 2;
constexpr int HIGHLIGHT_PADDING_Y = 1;
constexpr int HIGHLIGHT_RADIUS = 3;
constexpr unsigned long NAV_EDGE_DEBOUNCE_MS = 130;
constexpr unsigned long NAV_REPEAT_INITIAL_MS = 700;
constexpr unsigned long NAV_REPEAT_INTERVAL_MS = 95;
}  // namespace

void HighlightSelectionActivity::onEnter() {
  Activity::onEnter();
  invalidateSelectionRegionCache();
  extractWords();
  if (words.empty()) {
    GUI.drawPopup(renderer, tr(STR_EMPTY_CHAPTER));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    delay(700);
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  currentRow = std::min<int>(static_cast<int>(rows.size()) / 3, static_cast<int>(rows.size()) - 1);
  currentWordInRow = 0;
  selectionStarted = false;
  selectionAnchorWord = -1;
  selectionFocusWord = -1;
  hasStagedSelection = false;
  stagedSelection = {};
  storeSelectionBaseRegions();
  updateSelectionHighlight();
}

void HighlightSelectionActivity::onExit() {
  resetSelectionState();
  freeSelectionRegionCache();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  Activity::onExit();
}

void HighlightSelectionActivity::extractWords() {
  words.clear();
  rows.clear();
  if (!page) {
    return;
  }

  for (const auto& element : page->elements) {
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
      const int16_t width = static_cast<int16_t>(std::max(1, measureWordWidth(wordList[i].c_str())));
      words.push_back(HighlightGeometry::WordInfo{wordList[i], x, y, width, 0});
    }
  }

  if (words.empty()) {
    return;
  }

  std::sort(words.begin(), words.end(), [](const HighlightGeometry::WordInfo& a, const HighlightGeometry::WordInfo& b) {
    // Strict weak ordering (lexicographic). The +/-2px same-row tolerance must NOT
    // live in the comparator (non-transitive => std::sort UB); the row-grouping
    // pass below applies it linearly after the sort.
    if (a.screenY != b.screenY) {
      return a.screenY < b.screenY;
    }
    return a.screenX < b.screenX;
  });

  int16_t currentY = words[0].screenY;
  rows.push_back(HighlightGeometry::RowInfo{currentY, {}});
  for (size_t i = 0; i < words.size(); ++i) {
    if (std::abs(words[i].screenY - currentY) > 2) {
      currentY = words[i].screenY;
      rows.push_back(HighlightGeometry::RowInfo{currentY, {}});
    }
    words[i].row = static_cast<int16_t>(rows.size() - 1);
    rows.back().wordIndices.push_back(static_cast<int>(i));
  }
}

int HighlightSelectionActivity::measureWordWidth(const char* text) const {
  return renderer.getTextAdvanceX(readerFontId, text, EpdFontFamily::REGULAR);
}

void HighlightSelectionActivity::moveRow(const int delta) {
  if (rows.empty()) {
    return;
  }
  const int oldWordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const int oldCenter = words[static_cast<size_t>(oldWordIndex)].screenX + words[static_cast<size_t>(oldWordIndex)].width / 2;

  currentRow = (currentRow + delta + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
  int bestIndex = 0;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(rows[currentRow].wordIndices.size()); ++i) {
    const int wordIndex = rows[currentRow].wordIndices[static_cast<size_t>(i)];
    const int center = words[static_cast<size_t>(wordIndex)].screenX + words[static_cast<size_t>(wordIndex)].width / 2;
    const int distance = std::abs(center - oldCenter);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i;
    }
  }
  currentWordInRow = bestIndex;
  updateSelectionHighlight();
}

void HighlightSelectionActivity::moveWord(const int delta) {
  if (rows.empty()) {
    return;
  }
  const int rowCount = static_cast<int>(rows.size());
  const int wordCount = static_cast<int>(rows[currentRow].wordIndices.size());
  if (wordCount <= 0) {
    return;
  }

  if (delta < 0 && currentWordInRow > 0) {
    --currentWordInRow;
  } else if (delta > 0 && currentWordInRow + 1 < wordCount) {
    ++currentWordInRow;
  } else if (delta < 0) {
    currentRow = (currentRow + rowCount - 1) % rowCount;
    currentWordInRow = static_cast<int>(rows[currentRow].wordIndices.size()) - 1;
  } else {
    currentRow = (currentRow + 1) % rowCount;
    currentWordInRow = 0;
  }
  updateSelectionHighlight();
}

void HighlightSelectionActivity::startSelection() {
  if (rows.empty()) {
    return;
  }
  selectionStarted = true;
  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  selectionAnchorWord = wordIndex;
  selectionFocusWord = wordIndex;
  hasStagedSelection = false;
  stagedSelection = {};
}

void HighlightSelectionActivity::stopSelection() {
  if (!selectionStarted) {
    return;
  }
  stageSelection();
  selectionStarted = false;
  selectionAnchorWord = -1;
  selectionFocusWord = -1;
  currentRow = 0;
  currentWordInRow = 0;
}

void HighlightSelectionActivity::stageSelection() {
  if (words.empty()) {
    return;
  }
  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  selectionFocusWord = wordIndex;
  const int lo = std::min(selectionAnchorWord >= 0 ? selectionAnchorWord : wordIndex, wordIndex);
  const int hi = std::max(selectionAnchorWord >= 0 ? selectionAnchorWord : wordIndex, wordIndex);
  if (lo < 0 || hi < 0 || lo >= static_cast<int>(words.size()) || hi >= static_cast<int>(words.size()) || lo > hi) {
    return;
  }

  stagedSelection = buildCurrentResult();
  hasStagedSelection = true;
}

HighlightResult HighlightSelectionActivity::buildCurrentResult() const {
  HighlightResult result;
  if (words.empty() || rows.empty()) {
    return result;
  }

  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const int anchor = selectionAnchorWord >= 0 ? selectionAnchorWord : wordIndex;
  const int focus = selectionFocusWord >= 0 ? selectionFocusWord : wordIndex;
  const int lo = std::min(anchor, focus);
  const int hi = std::max(anchor, focus);
  if (lo < 0 || hi < 0 || lo >= static_cast<int>(words.size()) || hi >= static_cast<int>(words.size()) || lo > hi) {
    return result;
  }

  result.spineIndex = static_cast<uint16_t>(spineIndex);
  result.pageNumber = static_cast<uint16_t>(pageNumber);
  result.anchorWordIndex = static_cast<uint16_t>(anchor);
  result.focusWordIndex = static_cast<uint16_t>(focus);
  result.selectedText = collectSelectedText(lo, hi);
  result.createdAt = static_cast<uint32_t>(time(nullptr));
  return result;
}

std::string HighlightSelectionActivity::collectSelectedText(const int startWord, const int endWord) const {
  std::string out;
  if (startWord < 0 || endWord < 0 || startWord >= static_cast<int>(words.size()) ||
      endWord >= static_cast<int>(words.size()) || startWord > endWord) {
    return out;
  }

  for (int i = startWord; i <= endWord; ++i) {
    if (!out.empty()) {
      out.push_back(' ');
    }
    out += words[static_cast<size_t>(i)].text;
  }
  return out;
}

bool HighlightSelectionActivity::hasCurrentSelection() const {
  return selectionStarted || hasStagedSelection;
}

void HighlightSelectionActivity::resetSelectionState() {
  selectionStarted = false;
  selectionAnchorWord = -1;
  selectionFocusWord = -1;
  hasStagedSelection = false;
  stagedSelection = {};
  currentRow = 0;
  currentWordInRow = 0;
}

void HighlightSelectionActivity::saveSelection() {
  if (!hasStagedSelection) {
    if (selectionStarted) {
      stageSelection();
    }
  }
  if (!hasStagedSelection || stagedSelection.selectedText.empty()) {
    return;
  }
  setResult(ActivityResult{stagedSelection});
  selectionStarted = false;
  selectionAnchorWord = -1;
  selectionFocusWord = -1;
  hasStagedSelection = false;
  stagedSelection = {};
  currentRow = 0;
  currentWordInRow = 0;
  finish();
}

void HighlightSelectionActivity::cancelSelection() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  selectionStarted = false;
  selectionAnchorWord = -1;
  selectionFocusWord = -1;
  hasStagedSelection = false;
  stagedSelection = {};
  currentRow = 0;
  currentWordInRow = 0;
  finish();
}

void HighlightSelectionActivity::updateSelectionHighlight() {
  if (redrawSelectionFast()) {
    return;
  }
  requestUpdate();
}

bool HighlightSelectionActivity::redrawSelectionFast() {
  if (selectionRegionCount == 0) {
    return false;
  }

  RenderLock lock(*this);
  if (!restoreSelectionBaseRegions()) {
    return false;
  }
  if (!storeSelectionBaseRegions()) {
    return false;
  }

  drawSelectionHighlight();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

size_t HighlightSelectionActivity::collectSelectionRects(SelectionRect* rects, const size_t maxRects) const {
  if (!rects || maxRects == 0 || words.empty() || rows.empty()) {
    return 0;
  }

  const int focusWord = selectionStarted
                            ? rows[currentRow].wordIndices[currentWordInRow]
                            : (hasStagedSelection ? static_cast<int>(stagedSelection.focusWordIndex)
                                                  : rows[currentRow].wordIndices[currentWordInRow]);
  const int anchorWord = selectionStarted ? selectionAnchorWord
                                          : (hasStagedSelection ? static_cast<int>(stagedSelection.anchorWordIndex) : -1);
  const int lo = std::min(anchorWord >= 0 ? anchorWord : focusWord, focusWord);
  const int hi = std::max(anchorWord >= 0 ? anchorWord : focusWord, focusWord);
  if (lo < 0 || hi < 0 || lo >= static_cast<int>(words.size()) || hi >= static_cast<int>(words.size()) || lo > hi) {
    return 0;
  }

  size_t count = 0;
  const int lineHeight = renderer.getLineHeight(readerFontId);
  int i = lo;
  while (i <= hi && count < maxRects) {
    const int lineY = words[static_cast<size_t>(i)].screenY;
    int minX = words[static_cast<size_t>(i)].screenX;
    int maxR = words[static_cast<size_t>(i)].screenX + words[static_cast<size_t>(i)].width;
    int j = i + 1;
    while (j <= hi && words[static_cast<size_t>(j)].screenY == lineY) {
      minX = std::min(minX, static_cast<int>(words[static_cast<size_t>(j)].screenX));
      maxR = std::max(maxR, words[static_cast<size_t>(j)].screenX + words[static_cast<size_t>(j)].width);
      ++j;
    }
    rects[count++] = SelectionRect{minX - HIGHLIGHT_PADDING_X, lineY - HIGHLIGHT_PADDING_Y,
                                   std::max(1, maxR - minX + HIGHLIGHT_PADDING_X * 2),
                                   lineHeight + HIGHLIGHT_PADDING_Y * 2};
    i = j;
  }
  if (i <= hi) {
    // Too many line groups for the fixed region cache; fall back to a full redraw.
    return 0;
  }
  return count;
}

bool HighlightSelectionActivity::storeSelectionBaseRegions() {
  SelectionRect rects[MAX_SELECTION_REGIONS];
  const size_t rectCount = collectSelectionRects(rects, MAX_SELECTION_REGIONS);
  invalidateSelectionRegionCache();
  if (rectCount == 0) {
    return false;
  }

  for (size_t i = 0; i < rectCount; ++i) {
    const size_t required = renderer.getRegionByteSize(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
    if (required == 0) {
      invalidateSelectionRegionCache();
      return false;
    }

    SelectionRegionCache& region = selectionRegions[i];
    if (region.capacity < required) {
      uint8_t* replacement = static_cast<uint8_t*>(malloc(required));
      if (!replacement) {
        invalidateSelectionRegionCache();
        return false;
      }
      free(region.buffer);
      region.buffer = replacement;
      region.capacity = required;
    }

    if (!renderer.copyRegionToBuffer(rects[i].x, rects[i].y, rects[i].width, rects[i].height, region.buffer,
                                     region.capacity)) {
      invalidateSelectionRegionCache();
      return false;
    }

    region.rect = rects[i];
    region.size = required;
    region.stored = true;
  }

  selectionRegionCount = rectCount;
  return true;
}

bool HighlightSelectionActivity::restoreSelectionBaseRegions() const {
  if (selectionRegionCount == 0) {
    return false;
  }

  for (size_t i = 0; i < selectionRegionCount; ++i) {
    const SelectionRegionCache& region = selectionRegions[i];
    if (!region.stored || !region.buffer || region.size == 0) {
      return false;
    }
    if (!renderer.copyBufferToRegion(region.rect.x, region.rect.y, region.rect.width, region.rect.height,
                                     region.buffer, region.size)) {
      return false;
    }
  }
  return true;
}

void HighlightSelectionActivity::invalidateSelectionRegionCache() {
  selectionRegionCount = 0;
  for (auto& region : selectionRegions) {
    region.stored = false;
    region.size = 0;
  }
}

void HighlightSelectionActivity::freeSelectionRegionCache() {
  for (auto& region : selectionRegions) {
    free(region.buffer);
    region.buffer = nullptr;
    region.capacity = 0;
    region.size = 0;
    region.stored = false;
  }
  selectionRegionCount = 0;
}

void HighlightSelectionActivity::drawSelectionText(const HighlightGeometry::WordInfo& word, const bool inverted) const {
  renderer.fillRoundedRect(word.screenX - HIGHLIGHT_PADDING_X, word.screenY - HIGHLIGHT_PADDING_Y,
                           word.width + HIGHLIGHT_PADDING_X * 2, renderer.getLineHeight(readerFontId) + HIGHLIGHT_PADDING_Y * 2,
                           HIGHLIGHT_RADIUS, Color::Black);
  renderer.drawText(readerFontId, word.screenX, word.screenY, word.text.c_str(), !inverted);
}

void HighlightSelectionActivity::drawUnderlineForSelection(const HighlightGeometry::WordInfo& word) const {
  const int underlineY = word.screenY + renderer.getLineHeight(readerFontId) - 2;
  renderer.drawLine(word.screenX, underlineY, word.screenX + word.width - 1, underlineY, true);
}

void HighlightSelectionActivity::drawSelectionHighlight() {
  if (words.empty() || rows.empty()) {
    return;
  }

  const int focusWord = selectionStarted ? rows[currentRow].wordIndices[currentWordInRow]
                                         : (hasStagedSelection ? static_cast<int>(stagedSelection.focusWordIndex) : -1);
  const int anchorWord = selectionStarted ? selectionAnchorWord
                                          : (hasStagedSelection ? static_cast<int>(stagedSelection.anchorWordIndex) : -1);
  const int lo = std::min(anchorWord >= 0 ? anchorWord : focusWord, focusWord);
  const int hi = std::max(anchorWord >= 0 ? anchorWord : focusWord, focusWord);
  if (lo < 0 || hi < 0 || lo >= static_cast<int>(words.size()) || hi >= static_cast<int>(words.size()) || lo > hi) {
    return;
  }

  if (selectionStarted) {
    int i = lo;
    while (i <= hi) {
      const int lineY = words[static_cast<size_t>(i)].screenY;
      int j = i;
      while (j <= hi && words[static_cast<size_t>(j)].screenY == lineY) {
        drawSelectionText(words[static_cast<size_t>(j)], true);
        ++j;
      }
      i = j;
    }
    return;
  }

  if (hasStagedSelection) {
    int i = lo;
    while (i <= hi) {
      const int lineY = words[static_cast<size_t>(i)].screenY;
      int j = i;
      while (j <= hi && words[static_cast<size_t>(j)].screenY == lineY) {
        drawUnderlineForSelection(words[static_cast<size_t>(j)]);
        ++j;
      }
      i = j;
    }
    return;
  }

  if (currentWordInRow >= 0 && currentRow >= 0 && currentRow < static_cast<int>(rows.size()) &&
      currentWordInRow < static_cast<int>(rows[currentRow].wordIndices.size())) {
    const int wordIndex = rows[static_cast<size_t>(currentRow)].wordIndices[static_cast<size_t>(currentWordInRow)];
    drawUnderlineForSelection(words[static_cast<size_t>(wordIndex)]);
  }
}

void HighlightSelectionActivity::loop() {
  MappedInputManager& m = mappedInput;

  if (m.wasReleased(MappedInputManager::Button::Power)) {
    cancelSelection();
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    if (hasStagedSelection) {
      saveSelection();
    } else if (selectionStarted) {
      stopSelection();
      saveSelection();
    } else {
      cancelSelection();
    }
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!selectionStarted) {
      startSelection();
      updateSelectionHighlight();
      return;
    }
    stopSelection();
    updateSelectionHighlight();
    return;
  }

  if (m.wasPressed(MappedInputManager::Button::Left)) {
    moveWord(-1);
    return;
  }
  if (m.wasPressed(MappedInputManager::Button::Right)) {
    moveWord(1);
    return;
  }
  if (m.wasPressed(MappedInputManager::Button::Up)) {
    moveRow(-1);
    return;
  }
  if (m.wasPressed(MappedInputManager::Button::Down)) {
    moveRow(1);
    return;
  }
}

void HighlightSelectionActivity::render(RenderLock&&) {
  const auto orientation = renderer.getOrientation();
  drawSelectionHighlight();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  const char* backHint = hasStagedSelection ? tr(STR_SAVE_HIGHLIGHT) : tr(STR_EXIT);
  const char* confirmHint = selectionStarted ? tr(STR_STOP_SELECTION) : tr(STR_START_SELECTION);
  const auto labels = mappedInput.mapLabels(backHint, confirmHint, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.setOrientation(orientation);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
