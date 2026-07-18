#pragma once

#include <cstdint>
#include <string>

struct HighlightResult {
  std::string id;
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  uint16_t anchorWordIndex = 0;
  uint16_t focusWordIndex = 0;
  std::string selectedText;
  uint32_t createdAt = 0;
  std::string note;
};

