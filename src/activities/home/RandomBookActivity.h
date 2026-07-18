#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "../Activity.h"

class RandomBookActivity final : public Activity {
 public:
  explicit RandomBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RandomBook", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct PickState {
    uint32_t eligibleBooks = 0;
    std::string selectedPath;
    std::string fallbackPath;
  };

  std::unique_ptr<char[]> fileNameBuffer;
  bool noBooksFound = false;

  void scanDirectory(const std::string& directoryPath, PickState& state, int depth = 0);
  bool isReadableBook(const std::string& filename) const;
};
