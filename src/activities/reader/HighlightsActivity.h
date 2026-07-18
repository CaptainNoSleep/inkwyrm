#pragma once

#include <Epub.h>

#include <functional>
#include <memory>
#include <vector>

#include "../Activity.h"
#include "HighlightStore.h"
#include "util/ButtonNavigator.h"

class HighlightsActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::vector<HighlightStore::HighlightEntry> highlights;
  std::string headerTitle;
  std::function<bool(const HighlightStore::HighlightEntry&)> onDeleteHighlight;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;

  int getPageItems() const;
  std::string getItemLabel(int index) const;
  void confirmDeleteSelectedHighlight();

 public:
  explicit HighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::vector<HighlightStore::HighlightEntry>& highlights,
                              std::shared_ptr<Epub> epub = nullptr, std::string headerTitle = {},
                              std::function<bool(const HighlightStore::HighlightEntry&)> onDeleteHighlight = nullptr)
      : Activity("Highlights", renderer, mappedInput),
        epub(std::move(epub)),
        highlights(highlights),
        headerTitle(std::move(headerTitle)),
        onDeleteHighlight(std::move(onDeleteHighlight)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

