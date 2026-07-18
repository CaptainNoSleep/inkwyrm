#pragma once

#include <Epub/Page.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../Activity.h"
#include "HighlightTypes.h"
#include "HighlightGeometry.h"

class HighlightSelectionActivity final : public Activity {
 public:
  HighlightSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> page,
                             int readerFontId, int marginLeft, int marginTop, int spineIndex, int pageNumber)
      : Activity("HighlightSelection", renderer, mappedInput),
        page(std::move(page)),
        readerFontId(readerFontId),
        marginLeft(marginLeft),
        marginTop(marginTop),
        spineIndex(spineIndex),
        pageNumber(pageNumber) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  struct SelectionRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  struct SelectionRegionCache {
    SelectionRect rect;
    uint8_t* buffer = nullptr;
    size_t capacity = 0;
    size_t size = 0;
    bool stored = false;
  };

  static constexpr size_t MAX_SELECTION_REGIONS = 8;

  std::shared_ptr<Page> page;
  int readerFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  int spineIndex = 0;
  int pageNumber = 0;
  std::vector<HighlightGeometry::WordInfo> words;
  std::vector<HighlightGeometry::RowInfo> rows;
  int currentRow = 0;
  int currentWordInRow = 0;
  bool selectionStarted = false;
  int selectionAnchorWord = -1;
  int selectionFocusWord = -1;
  bool hasStagedSelection = false;
  HighlightResult stagedSelection;
  SelectionRegionCache selectionRegions[MAX_SELECTION_REGIONS];
  size_t selectionRegionCount = 0;

  void extractWords();
  int measureWordWidth(const char* text) const;
  void moveRow(int delta);
  void moveWord(int delta);
  void startSelection();
  void stopSelection();
  void saveSelection();
  void cancelSelection();
  void updateSelectionHighlight();
  bool redrawSelectionFast();
  size_t collectSelectionRects(SelectionRect* rects, size_t maxRects) const;
  bool storeSelectionBaseRegions();
  bool restoreSelectionBaseRegions() const;
  void invalidateSelectionRegionCache();
  void freeSelectionRegionCache();
  void drawSelectionHighlight();
  void drawSelectionText(const HighlightGeometry::WordInfo& word, bool inverted) const;
  void drawUnderlineForSelection(const HighlightGeometry::WordInfo& word) const;
  void stageSelection();
  HighlightResult buildCurrentResult() const;
  std::string collectSelectedText(int startWord, int endWord) const;
  bool hasCurrentSelection() const;
  void resetSelectionState();
};
