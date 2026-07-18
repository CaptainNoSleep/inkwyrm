#include "RandomBookActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <cstring>
#include <esp_random.h>

#include "CrossPointSettings.h"
#include "activities/reader/ReaderActivity.h"
#include "fontIds.h"

namespace {
constexpr size_t NAME_BUFFER_SIZE = 500;
std::string lastRandomBookPath;

std::string joinPath(const std::string& directoryPath, const char* name) {
  return directoryPath == "/" ? std::string("/") + name : directoryPath + "/" + name;
}
}  // namespace

void RandomBookActivity::onEnter() {
  Activity::onEnter();
  noBooksFound = false;
  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    noBooksFound = true;
    requestUpdate();
    return;
  }

  PickState state;
  scanDirectory("/", state);
  const std::string path = !state.selectedPath.empty() ? state.selectedPath : state.fallbackPath;
  fileNameBuffer.reset();
  if (path.empty()) {
    noBooksFound = true;
    requestUpdate();
    return;
  }

  lastRandomBookPath = path;
  activityManager.replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, path));
}

void RandomBookActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void RandomBookActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2 - 10,
                            noBooksFound ? tr(STR_NO_FILES_FOUND) : tr(STR_LOADING));
  renderer.displayBuffer();
}

bool RandomBookActivity::isReadableBook(const std::string& filename) const {
  return FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
         FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename);
}

void RandomBookActivity::scanDirectory(const std::string& directoryPath, PickState& state, const int depth) {
  // Recursive over the SD tree; real cards are /Books/<Author>/ (2 deep). Cap
  // guards the small ESP32-C3 stack against pathological nesting.
  constexpr int MAX_SCAN_DEPTH = 8;
  if (depth > MAX_SCAN_DEPTH) {
    return;
  }
  auto dir = Storage.open(directoryPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const char* name = fileNameBuffer.get();
    if ((!SETTINGS.showHiddenFiles && name[0] == '.') || strcmp(name, "System Volume Information") == 0 ||
        strcmp(name, ".crosspoint") == 0) {
      entry.close();
      continue;
    }

    const std::string path = joinPath(directoryPath, name);
    if (entry.isDirectory()) {
      entry.close();
      scanDirectory(path, state, depth + 1);
      continue;
    }

    if (isReadableBook(name)) {
      if (state.fallbackPath.empty()) state.fallbackPath = path;
      if (path != lastRandomBookPath && (esp_random() % ++state.eligibleBooks) == 0) state.selectedPath = path;
    }
    entry.close();
  }
  dir.close();
}
