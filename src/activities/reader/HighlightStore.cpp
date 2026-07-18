#include "HighlightStore.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <ctime>

#include "JsonSettingsIO.h"

namespace {
std::string makeStableHighlightPath(const std::string& bookId) {
  return BookIdentity::getStableDataFilePath(bookId, "highlights.json");
}
}  // namespace

void HighlightStore::load(const std::string& cachePath, const std::string& bookIdIn) {
  storagePath.clear();
  legacyPath.clear();
  bookId = bookIdIn;
  if (!bookId.empty()) {
    BookIdentity::ensureStableDataDir(bookId);
    storagePath = makeStableHighlightPath(bookId);
    if (!cachePath.empty()) {
      legacyPath = cachePath + "/" + LEGACY_FILE_NAME;
    }
  } else if (!cachePath.empty()) {
    storagePath = cachePath + "/" + LEGACY_FILE_NAME;
  }

  highlights.clear();
  dirty = false;

  if (!storagePath.empty() && Storage.exists(storagePath.c_str()) && JsonSettingsIO::loadHighlightsFromFile(*this, storagePath.c_str())) {
    return;
  }

  if (!legacyPath.empty() && legacyPath != storagePath && Storage.exists(legacyPath.c_str()) &&
      JsonSettingsIO::loadHighlightsFromFile(*this, legacyPath.c_str())) {
    dirty = true;
    save();
  }
}

void HighlightStore::save() {
  if (!dirty || storagePath.empty()) {
    return;
  }
  BookIdentity::ensureStableDataDir(bookId);
  if (JsonSettingsIO::saveHighlights(*this, storagePath.c_str())) {
    dirty = false;
  }
}

bool HighlightStore::add(const HighlightResult& highlight) {
  HighlightEntry entry;
  entry.id = highlight.id.empty() ? makeHighlightId(highlight.createdAt ? highlight.createdAt : static_cast<uint32_t>(time(nullptr)))
                                  : highlight.id;
  entry.bookId = bookId;
  entry.spineIndex = highlight.spineIndex;
  entry.pageNumber = highlight.pageNumber;
  entry.anchorWordIndex = highlight.anchorWordIndex;
  entry.focusWordIndex = highlight.focusWordIndex;
  entry.selectedText = highlight.selectedText;
  entry.createdAt = highlight.createdAt ? highlight.createdAt : static_cast<uint32_t>(time(nullptr));
  entry.note = highlight.note;

  if (entry.selectedText.empty()) {
    return false;
  }

  highlights.push_back(std::move(entry));
  dirty = true;
  return true;
}

bool HighlightStore::removeById(const std::string& id) {
  const auto it = std::find_if(highlights.begin(), highlights.end(), [&](const HighlightEntry& entry) {
    return entry.id == id;
  });
  if (it == highlights.end()) {
    return false;
  }
  highlights.erase(it);
  dirty = true;
  return true;
}

bool HighlightStore::hasDuplicate(const HighlightResult& highlight) const {
  return std::any_of(highlights.begin(), highlights.end(), [&](const HighlightEntry& entry) {
    return entry.spineIndex == highlight.spineIndex && entry.pageNumber == highlight.pageNumber &&
           entry.anchorWordIndex == highlight.anchorWordIndex && entry.focusWordIndex == highlight.focusWordIndex &&
           entry.selectedText == highlight.selectedText;
  });
}


std::string HighlightStore::makeHighlightId(const uint32_t createdAt) const {
  return bookId + "-" + std::to_string(createdAt) + "-" + std::to_string(highlights.size() + 1);
}
