#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "util/BookIdentity.h"

// Word-index invariant (must match HighlightSelectActivity::extractWords and
// EpubReaderActivity::drawHighlightUnderlines): startWord/endWord are a running
// count over page->elements in element order, over each TAG_PageLine's
// block->getWords() (min(words.size(), wordXpos.size()) entries), skipping
// nothing. Both sites must walk pages identically or ranges will desync.
class HighlightStore {
 public:
  struct Highlight {
    uint16_t spineIndex = 0;
    uint16_t startPage = 0;
    uint16_t startWord = 0;
    uint16_t endPage = 0;
    uint16_t endWord = 0;  // inclusive
    std::string snippet;
  };

  void load(const std::string& bookId) {
    storagePath.clear();
    highlights.clear();
    dirty = false;

    if (bookId.empty()) {
      return;
    }

    BookIdentity::ensureStableDataDir(bookId);
    storagePath = BookIdentity::getStableDataFilePath(bookId, "highlights.bin");

    FsFile file;
    if (!Storage.openFileForRead("HLT", storagePath, file)) {
      return;
    }

    uint8_t version = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) != sizeof(version) || version < 1 ||
        version > FILE_VERSION) {
      file.close();
      return;
    }

    uint32_t count = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) != sizeof(count)) {
      file.close();
      return;
    }

    highlights.reserve(static_cast<size_t>(count));
    for (uint32_t index = 0; index < count; ++index) {
      Highlight highlight;
      if (!readU16(file, highlight.spineIndex) || !readU16(file, highlight.startPage) ||
          !readU16(file, highlight.startWord) || !readU16(file, highlight.endPage) ||
          !readU16(file, highlight.endWord)) {
        highlights.clear();
        file.close();
        return;
      }

      uint8_t snippetLen = 0;
      if (file.read(&snippetLen, 1) == 1 && snippetLen > 0) {
        char buffer[MAX_SNIPPET_LEN + 1];
        const uint8_t toRead = std::min(snippetLen, static_cast<uint8_t>(MAX_SNIPPET_LEN));
        if (file.read(reinterpret_cast<uint8_t*>(buffer), toRead) == toRead) {
          buffer[toRead] = '\0';
          highlight.snippet = buffer;
        }
        if (snippetLen > toRead) {
          file.seekCur(snippetLen - toRead);
        }
      }

      highlights.push_back(std::move(highlight));
    }

    file.close();
  }

  void save() {
    if (!dirty || storagePath.empty()) {
      return;
    }

    FsFile file;
    if (!Storage.openFileForWrite("HLT", storagePath, file)) {
      LOG_ERR("HLT", "Failed to save highlights");
      return;
    }

    auto writePodChecked = [&file](const auto& value) {
      return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
    };

    const uint32_t count = static_cast<uint32_t>(highlights.size());
    bool ok = writePodChecked(FILE_VERSION) && writePodChecked(count);

    for (const auto& highlight : highlights) {
      ok = ok && writePodChecked(highlight.spineIndex) && writePodChecked(highlight.startPage) &&
           writePodChecked(highlight.startWord) && writePodChecked(highlight.endPage) &&
           writePodChecked(highlight.endWord);
      const uint8_t snippetLen =
          static_cast<uint8_t>(std::min(highlight.snippet.size(), static_cast<size_t>(MAX_SNIPPET_LEN)));
      ok = ok && writePodChecked(snippetLen);
      if (snippetLen > 0) {
        ok = ok && file.write(reinterpret_cast<const uint8_t*>(highlight.snippet.c_str()), snippetLen) == snippetLen;
      }
    }

    ok = ok && file.close();
    if (!ok) {
      LOG_ERR("HLT", "Failed while writing highlights");
      return;
    }

    dirty = false;
  }

  void add(Highlight highlight) {
    highlight.snippet = highlight.snippet.substr(0, MAX_SNIPPET_LEN);
    highlights.push_back(std::move(highlight));
    dirty = true;
  }

  bool remove(const Highlight& target) {
    auto it = std::find_if(highlights.begin(), highlights.end(), [&target](const Highlight& current) {
      return current.spineIndex == target.spineIndex && current.startPage == target.startPage &&
             current.startWord == target.startWord && current.endPage == target.endPage &&
             current.endWord == target.endWord;
    });
    if (it == highlights.end()) {
      return false;
    }

    highlights.erase(it);
    dirty = true;
    return true;
  }

  void clear() {
    if (highlights.empty()) {
      return;
    }
    highlights.clear();
    dirty = true;
  }

  [[nodiscard]] const std::vector<Highlight>& getAll() const { return highlights; }
  [[nodiscard]] bool isEmpty() const { return highlights.empty(); }

  [[nodiscard]] bool hasAnyForSpine(const uint16_t spineIndex) const {
    return std::any_of(highlights.begin(), highlights.end(),
                       [spineIndex](const Highlight& highlight) { return highlight.spineIndex == spineIndex; });
  }

 private:
  static constexpr uint8_t FILE_VERSION = 1;
  static constexpr uint8_t MAX_SNIPPET_LEN = 120;

  std::vector<Highlight> highlights;
  std::string storagePath;
  bool dirty = false;

  static bool readU16(FsFile& file, uint16_t& value) {
    return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(value)) == sizeof(value);
  }
};
