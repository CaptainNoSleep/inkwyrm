#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "HighlightTypes.h"
#include "util/BookIdentity.h"

class HighlightStore;

namespace JsonSettingsIO {
bool saveHighlights(const HighlightStore& store, const char* path);
bool loadHighlights(HighlightStore& store, const char* json);
bool loadHighlightsFromFile(HighlightStore& store, const char* path);
}  // namespace JsonSettingsIO

class HighlightStore {
 public:
  struct HighlightEntry {
    std::string id;
    std::string bookId;
    uint16_t spineIndex = 0;
    uint16_t pageNumber = 0;
    uint16_t anchorWordIndex = 0;
    uint16_t focusWordIndex = 0;
    std::string selectedText;
    uint32_t createdAt = 0;
    std::string note;
  };

  void load(const std::string& cachePath, const std::string& bookId = "");
  void save();

  bool add(const HighlightResult& highlight);
  bool removeById(const std::string& id);
  bool hasDuplicate(const HighlightResult& highlight) const;

  [[nodiscard]] const std::vector<HighlightEntry>& getAll() const { return highlights; }
  [[nodiscard]] bool isEmpty() const { return highlights.empty(); }
  [[nodiscard]] const std::string& getBookId() const { return bookId; }


  friend bool JsonSettingsIO::saveHighlights(const HighlightStore&, const char*);
  friend bool JsonSettingsIO::loadHighlights(HighlightStore&, const char*);
  friend bool JsonSettingsIO::loadHighlightsFromFile(HighlightStore&, const char*);

 private:
  static constexpr uint32_t FILE_VERSION = 1;
  static constexpr const char* FILE_NAME = "highlights.json";
  static constexpr const char* LEGACY_FILE_NAME = "highlights.json";

  std::vector<HighlightEntry> highlights;
  std::string storagePath;
  std::string legacyPath;
  std::string bookId;
  bool dirty = false;

  [[nodiscard]] std::string makeHighlightId(uint32_t createdAt) const;
};
