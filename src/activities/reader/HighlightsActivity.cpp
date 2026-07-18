#include "HighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long DELETE_HIGHLIGHT_HOLD_MS = 1000;
}

int HighlightsActivity::getPageItems() const {
  constexpr int lineHeight = 30;
  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - lineHeight;
  return std::max(1, availableHeight / lineHeight);
}

std::string HighlightsActivity::getItemLabel(const int index) const {
  const auto& highlight = highlights[index];
  char buffer[64];

  if (!highlight.selectedText.empty()) {
    snprintf(buffer, sizeof(buffer), "%d. ", index + 1);
    return std::string(buffer) + highlight.selectedText;
  }

  if (epub) {
    const int tocIndex = epub->getTocIndexForSpineIndex(highlight.spineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      snprintf(buffer, sizeof(buffer), "%d. ", index + 1);
      return std::string(buffer) + tocItem.title + " - Page " + std::to_string(highlight.pageNumber + 1);
    }
  }

  snprintf(buffer, sizeof(buffer), "%d. Page %d", index + 1, highlight.pageNumber + 1);
  return buffer;
}

void HighlightsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void HighlightsActivity::onExit() { Activity::onExit(); }

void HighlightsActivity::confirmDeleteSelectedHighlight() {
  if (!onDeleteHighlight || selectorIndex < 0 || selectorIndex >= static_cast<int>(highlights.size())) {
    return;
  }

  const auto highlight = highlights[selectorIndex];
  const std::string body = getItemLabel(selectorIndex);
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_HIGHLIGHT), body),
      [this, highlight](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        if (onDeleteHighlight(highlight)) {
          highlights.erase(std::remove_if(highlights.begin(), highlights.end(),
                                          [&](const HighlightStore::HighlightEntry& current) {
                                            return current.id == highlight.id;
                                          }),
                           highlights.end());

          if (highlights.empty()) {
            ActivityResult cancelResult;
            cancelResult.isCancelled = true;
            setResult(std::move(cancelResult));
            finish();
            return;
          }

          if (selectorIndex >= static_cast<int>(highlights.size())) {
            selectorIndex = static_cast<int>(highlights.size()) - 1;
          }
        }

        requestUpdate();
      });
}

void HighlightsActivity::loop() {
  const int totalItems = static_cast<int>(highlights.size());
  const int pageItems = getPageItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= DELETE_HIGHLIGHT_HOLD_MS) {
      confirmDeleteSelectedHighlight();
      return;
    }

    if (!highlights.empty()) {
      const auto& highlight = highlights[selectorIndex];
      setResult(BookmarkResult{static_cast<int>(highlight.spineIndex), highlight.pageNumber});
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void HighlightsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int totalItems = static_cast<int>(highlights.size());
  if (totalItems == 0) {
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_NO_HIGHLIGHTS), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;
  const int pageItems = getPageItems();

  const char* rawTitle = headerTitle.empty() ? tr(STR_HIGHLIGHTS) : headerTitle.c_str();
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, rawTitle, contentWidth - 20);
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, title.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, title.c_str(), true, EpdFontFamily::BOLD);

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  renderer.fillRect(contentX, 60 + contentY + (selectorIndex % pageItems) * 30 - 2, contentWidth - 1, 30);

  for (int i = 0; i < pageItems; ++i) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }

    const int displayY = 60 + contentY + i * 30;
    const bool isSelected = itemIndex == selectorIndex;
    const auto& highlight = highlights[static_cast<size_t>(itemIndex)];
    const std::string truncated =
        renderer.truncatedText(UI_10_FONT_ID, getItemLabel(itemIndex).c_str(), contentWidth - 40);
    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, truncated.c_str(), !isSelected);
    if (!highlight.note.empty()) {
      const std::string note = renderer.truncatedText(UI_10_FONT_ID, highlight.note.c_str(), contentWidth - 40);
      renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY + 14, note.c_str(), !isSelected);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

