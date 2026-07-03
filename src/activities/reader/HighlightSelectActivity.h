#pragma once

#include <Epub/Page.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Activity.h"

// Two-phase word-range selection UX modeled on DictionaryWordSelectActivity:
// pick a start word, then an end word (possibly on a later page within the
// same section), then save the range via onSave. Word-index invariant shared
// with HighlightStore/EpubReaderActivity::drawHighlightUnderlines: pageWordIndex
// is a running count over page->elements in order, over each TAG_PageLine's
// block->getWords() (min(words,xpos) entries), skipping nothing.
class HighlightSelectActivity final : public Activity {
 public:
  using PageLoader = std::function<std::shared_ptr<Page>(uint16_t pageNumber)>;
  using SaveCallback = std::function<void(uint16_t startPage, uint16_t startWord, uint16_t endPage, uint16_t endWord,
                                          const std::string& snippet)>;

  HighlightSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Page> initialPage,
                          uint16_t initialPageNumber, uint16_t sectionPageCount, int readerFontId, int marginLeft,
                          int marginTop, PageLoader pageLoader, SaveCallback onSave)
      : Activity("HighlightSelect", renderer, mappedInput),
        page(std::move(initialPage)),
        currentPageNumber(initialPageNumber),
        sectionPageCount(sectionPageCount),
        readerFontId(readerFontId),
        marginLeft(marginLeft),
        marginTop(marginTop),
        pageLoader(std::move(pageLoader)),
        onSave(std::move(onSave)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  enum class Phase { PickStart, PickEnd };

  struct WordInfo {
    std::string text;
    int16_t screenX = 0;
    int16_t screenY = 0;
    int16_t width = 0;
    int16_t row = 0;
    uint16_t pageWordIndex = 0;
  };

  struct Row {
    int16_t y = 0;
    std::vector<int> wordIndices;
  };

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

  static constexpr size_t MAX_SELECTION_REGIONS = 2;

  std::shared_ptr<Page> page;
  uint16_t currentPageNumber = 0;
  uint16_t sectionPageCount = 1;
  int readerFontId = 0;
  int marginLeft = 0;
  int marginTop = 0;
  PageLoader pageLoader;
  SaveCallback onSave;

  std::vector<WordInfo> words;
  std::vector<Row> rows;
  int currentRow = 0;
  int currentWordInRow = 0;

  Phase phase = Phase::PickStart;
  uint16_t anchorPage = 0;
  uint16_t anchorWord = 0;

  SelectionRegionCache selectionRegions[MAX_SELECTION_REGIONS];
  size_t selectionRegionCount = 0;

  void extractWords();
  void prepareReaderFontMetrics();
  int measureWordWidth(const char* text) const;
  void moveRow(int delta);
  void moveWord(int delta);
  void goToPage(uint16_t pageNumber, const uint16_t* preferPageWordIndex, bool cursorAtEnd);
  int findWordArrayIndex(uint16_t pageWordIndex) const;
  void updateSelectionHighlight();
  bool redrawSelectionFast();
  void prewarmCurrentSelectionText() const;
  size_t collectSelectionRects(SelectionRect* rects, size_t maxRects) const;
  bool storeSelectionBaseRegions();
  bool restoreSelectionBaseRegions() const;
  void invalidateSelectionRegionCache();
  void freeSelectionRegionCache();
  void drawSelectionHighlight();
  void saveHighlight(uint16_t selectedPage, uint16_t selectedWord);
  std::string buildSnippet(uint16_t startPage, uint16_t startWord, uint16_t endPage, uint16_t endWord) const;
};
