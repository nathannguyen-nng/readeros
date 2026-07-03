#include "HighlightSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <optional>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int HIGHLIGHT_PADDING_X = 2;
constexpr int HIGHLIGHT_PADDING_Y = 1;
constexpr int HIGHLIGHT_RADIUS = 3;
constexpr size_t SNIPPET_BUILD_LIMIT = 120;
}  // namespace

void HighlightSelectActivity::onEnter() {
  Activity::onEnter();
  invalidateSelectionRegionCache();
  extractWords();
  if (!rows.empty()) {
    currentRow = std::min<int>(static_cast<int>(rows.size()) / 3, static_cast<int>(rows.size()) - 1);
    currentWordInRow = 0;
  }
  requestUpdate();
}

void HighlightSelectActivity::onExit() {
  freeSelectionRegionCache();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }
  Activity::onExit();
}

void HighlightSelectActivity::extractWords() {
  words.clear();
  rows.clear();
  if (!page) return;

  prepareReaderFontMetrics();

  uint16_t runningIndex = 0;
  for (const auto& element : page->elements) {
    if (!element || element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) continue;

    const auto& wordList = block->getWords();
    const auto& xPositions = block->getWordXpos();
    const size_t count = std::min(wordList.size(), xPositions.size());
    for (size_t i = 0; i < count; ++i) {
      const int16_t x = static_cast<int16_t>(line.xPos + xPositions[i] + marginLeft);
      const int16_t y = static_cast<int16_t>(line.yPos + marginTop);
      const int16_t width = static_cast<int16_t>(std::max(1, measureWordWidth(wordList[i].c_str())));
      words.push_back(WordInfo{wordList[i], x, y, width, 0, runningIndex});
      ++runningIndex;
    }
  }

  if (words.empty()) return;
  std::sort(words.begin(), words.end(), [](const WordInfo& a, const WordInfo& b) {
    if (std::abs(a.screenY - b.screenY) > 2) return a.screenY < b.screenY;
    return a.screenX < b.screenX;
  });

  int16_t currentY = words[0].screenY;
  rows.push_back(Row{currentY, {}});
  for (size_t i = 0; i < words.size(); ++i) {
    if (std::abs(words[i].screenY - currentY) > 2) {
      currentY = words[i].screenY;
      rows.push_back(Row{currentY, {}});
    }
    words[i].row = static_cast<int16_t>(rows.size() - 1);
    rows.back().wordIndices.push_back(static_cast<int>(i));
  }
}

void HighlightSelectActivity::prepareReaderFontMetrics() {
  if (!page || !renderer.isSdCardFont(readerFontId)) return;

  std::string pageText;
  pageText.reserve(2048);
  for (const auto& element : page->elements) {
    if (!element || element->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*element);
    const auto& block = line.getBlock();
    if (!block) continue;

    const auto& wordList = block->getWords();
    for (const auto& word : wordList) {
      if (!pageText.empty()) pageText.push_back(' ');
      pageText += word;
    }
  }

  if (!pageText.empty()) {
    renderer.ensureSdCardFontReady(readerFontId, pageText.c_str(), 0x01);
  }
}

int HighlightSelectActivity::measureWordWidth(const char* text) const {
  return renderer.getTextAdvanceX(readerFontId, text, EpdFontFamily::REGULAR);
}

int HighlightSelectActivity::findWordArrayIndex(const uint16_t pageWordIndex) const {
  for (size_t i = 0; i < words.size(); ++i) {
    if (words[i].pageWordIndex == pageWordIndex) return static_cast<int>(i);
  }
  return -1;
}

void HighlightSelectActivity::goToPage(const uint16_t pageNumber, const uint16_t* preferPageWordIndex,
                                       const bool cursorAtEnd) {
  auto placeCursor = [this, preferPageWordIndex, cursorAtEnd] {
    if (preferPageWordIndex) {
      const int found = findWordArrayIndex(*preferPageWordIndex);
      if (found >= 0) {
        currentRow = words[found].row;
        const auto& indices = rows[currentRow].wordIndices;
        const auto it = std::find(indices.begin(), indices.end(), found);
        currentWordInRow = it != indices.end() ? static_cast<int>(it - indices.begin()) : 0;
        return;
      }
    }
    if (rows.empty()) {
      currentRow = 0;
      currentWordInRow = 0;
      return;
    }
    currentRow = cursorAtEnd ? static_cast<int>(rows.size()) - 1 : 0;
    currentWordInRow = cursorAtEnd ? static_cast<int>(rows[currentRow].wordIndices.size()) - 1 : 0;
  };

  if (pageNumber == currentPageNumber && page) {
    // Always a full render here (not the fast selection-only redraw): this path is used to
    // revert to the anchor word after Back, where the phase-dependent button hint also changes.
    invalidateSelectionRegionCache();
    placeCursor();
    requestUpdate();
    return;
  }

  auto newPage = pageLoader(pageNumber);
  if (!newPage) return;  // stay on current page

  {
    RenderLock lock(*this);
    invalidateSelectionRegionCache();
    page = std::move(newPage);
    currentPageNumber = pageNumber;
    extractWords();
    placeCursor();
  }
  requestUpdate();
}

void HighlightSelectActivity::moveRow(const int delta) {
  if (rows.empty()) {
    if (delta > 0 && currentPageNumber + 1 < sectionPageCount) {
      goToPage(static_cast<uint16_t>(currentPageNumber + 1), nullptr, false);
    } else if (delta < 0 && currentPageNumber > 0) {
      goToPage(static_cast<uint16_t>(currentPageNumber - 1), nullptr, true);
    }
    return;
  }

  const int oldWordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const int oldCenter = words[oldWordIndex].screenX + words[oldWordIndex].width / 2;

  const int newRow = currentRow + delta;
  if (newRow < 0) {
    if (currentPageNumber == 0) return;
    goToPage(static_cast<uint16_t>(currentPageNumber - 1), nullptr, true);
    return;
  }
  if (newRow >= static_cast<int>(rows.size())) {
    if (currentPageNumber + 1 >= sectionPageCount) return;
    goToPage(static_cast<uint16_t>(currentPageNumber + 1), nullptr, false);
    return;
  }

  currentRow = newRow;
  int bestIndex = 0;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(rows[currentRow].wordIndices.size()); ++i) {
    const int wordIndex = rows[currentRow].wordIndices[i];
    const int center = words[wordIndex].screenX + words[wordIndex].width / 2;
    const int distance = std::abs(center - oldCenter);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i;
    }
  }
  currentWordInRow = bestIndex;
  updateSelectionHighlight();
}

void HighlightSelectActivity::moveWord(const int delta) {
  if (rows.empty()) return;
  const int rowCount = static_cast<int>(rows.size());
  const int wordCount = static_cast<int>(rows[currentRow].wordIndices.size());
  if (wordCount <= 0) return;

  if (delta < 0 && currentWordInRow > 0) {
    --currentWordInRow;
  } else if (delta > 0 && currentWordInRow + 1 < wordCount) {
    ++currentWordInRow;
  } else if (delta < 0) {
    currentRow = (currentRow + rowCount - 1) % rowCount;
    currentWordInRow = static_cast<int>(rows[currentRow].wordIndices.size()) - 1;
  } else {
    currentRow = (currentRow + 1) % rowCount;
    currentWordInRow = 0;
  }
  updateSelectionHighlight();
}

void HighlightSelectActivity::updateSelectionHighlight() {
  if (redrawSelectionFast()) return;
  requestUpdate();
}

bool HighlightSelectActivity::redrawSelectionFast() {
  if (selectionRegionCount == 0) return false;

  RenderLock lock(*this);
  if (!restoreSelectionBaseRegions()) return false;
  if (!storeSelectionBaseRegions()) return false;

  prewarmCurrentSelectionText();
  drawSelectionHighlight();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  return true;
}

void HighlightSelectActivity::prewarmCurrentSelectionText() const {
  if (rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) || currentWordInRow < 0 ||
      currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return;
  }

  auto* fcm = renderer.getFontCacheManager();
  if (!fcm) return;

  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  std::string text = words[wordIndex].text;
  if (phase == Phase::PickEnd && anchorPage == currentPageNumber) {
    const int anchorIndex = findWordArrayIndex(anchorWord);
    if (anchorIndex >= 0 && anchorIndex != wordIndex) {
      text.push_back(' ');
      text += words[anchorIndex].text;
    }
  }

  if (!text.empty()) {
    fcm->prewarmCache(readerFontId, text.c_str(), 0x01);
  }
}

size_t HighlightSelectActivity::collectSelectionRects(SelectionRect* rects, const size_t maxRects) const {
  if (!rects || maxRects == 0 || rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) ||
      currentWordInRow < 0 || currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return 0;
  }

  auto addRect = [&](const WordInfo& selectedWord, size_t& count) {
    if (count >= maxRects) return;
    const int lineHeight = renderer.getLineHeight(readerFontId);
    rects[count++] = SelectionRect{selectedWord.screenX - HIGHLIGHT_PADDING_X,
                                   selectedWord.screenY - HIGHLIGHT_PADDING_Y,
                                   selectedWord.width + HIGHLIGHT_PADDING_X * 2,
                                   lineHeight + HIGHLIGHT_PADDING_Y * 2};
  };

  size_t count = 0;
  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  addRect(words[wordIndex], count);

  if (phase == Phase::PickEnd && anchorPage == currentPageNumber) {
    const int anchorIndex = findWordArrayIndex(anchorWord);
    if (anchorIndex >= 0 && anchorIndex != wordIndex) {
      addRect(words[anchorIndex], count);
    }
  }

  return count;
}

bool HighlightSelectActivity::storeSelectionBaseRegions() {
  SelectionRect rects[MAX_SELECTION_REGIONS];
  const size_t rectCount = collectSelectionRects(rects, MAX_SELECTION_REGIONS);
  invalidateSelectionRegionCache();
  if (rectCount == 0) return false;

  for (size_t i = 0; i < rectCount; ++i) {
    const size_t required = renderer.getRegionByteSize(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
    if (required == 0) {
      invalidateSelectionRegionCache();
      return false;
    }

    SelectionRegionCache& region = selectionRegions[i];
    if (region.capacity < required) {
      uint8_t* replacement = static_cast<uint8_t*>(malloc(required));
      if (!replacement) {
        invalidateSelectionRegionCache();
        return false;
      }
      free(region.buffer);
      region.buffer = replacement;
      region.capacity = required;
    }

    if (!renderer.copyRegionToBuffer(rects[i].x, rects[i].y, rects[i].width, rects[i].height, region.buffer,
                                     region.capacity)) {
      invalidateSelectionRegionCache();
      return false;
    }

    region.rect = rects[i];
    region.size = required;
    region.stored = true;
  }

  selectionRegionCount = rectCount;
  return true;
}

bool HighlightSelectActivity::restoreSelectionBaseRegions() const {
  if (selectionRegionCount == 0) return false;

  for (size_t i = 0; i < selectionRegionCount; ++i) {
    const SelectionRegionCache& region = selectionRegions[i];
    if (!region.stored || !region.buffer || region.size == 0) return false;
    if (!renderer.copyBufferToRegion(region.rect.x, region.rect.y, region.rect.width, region.rect.height,
                                     region.buffer, region.size)) {
      return false;
    }
  }
  return true;
}

void HighlightSelectActivity::invalidateSelectionRegionCache() {
  selectionRegionCount = 0;
  for (auto& region : selectionRegions) {
    region.stored = false;
    region.size = 0;
  }
}

void HighlightSelectActivity::freeSelectionRegionCache() {
  for (auto& region : selectionRegions) {
    free(region.buffer);
    region.buffer = nullptr;
    region.capacity = 0;
    region.size = 0;
    region.stored = false;
  }
  selectionRegionCount = 0;
}

void HighlightSelectActivity::drawSelectionHighlight() {
  if (rows.empty() || currentRow < 0 || currentRow >= static_cast<int>(rows.size()) || currentWordInRow < 0 ||
      currentWordInRow >= static_cast<int>(rows[currentRow].wordIndices.size())) {
    return;
  }

  const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
  const int lineHeight = renderer.getLineHeight(readerFontId);

  auto drawWord = [&](const WordInfo& selectedWord) {
    renderer.fillRoundedRect(selectedWord.screenX - HIGHLIGHT_PADDING_X, selectedWord.screenY - HIGHLIGHT_PADDING_Y,
                             selectedWord.width + HIGHLIGHT_PADDING_X * 2, lineHeight + HIGHLIGHT_PADDING_Y * 2,
                             HIGHLIGHT_RADIUS, Color::Black);
    renderer.drawText(readerFontId, selectedWord.screenX, selectedWord.screenY, selectedWord.text.c_str(), false);
  };

  drawWord(words[wordIndex]);

  if (phase == Phase::PickEnd && anchorPage == currentPageNumber) {
    const int anchorIndex = findWordArrayIndex(anchorWord);
    if (anchorIndex >= 0 && anchorIndex != wordIndex) {
      drawWord(words[anchorIndex]);
    }
  }
}

std::string HighlightSelectActivity::buildSnippet(const uint16_t startPage, const uint16_t startWord,
                                                   const uint16_t endPage, const uint16_t endWord) const {
  std::string snippet;
  snippet.reserve(SNIPPET_BUILD_LIMIT + 8);

  for (uint16_t p = startPage; p <= endPage; ++p) {
    const std::shared_ptr<Page> pg = (p == currentPageNumber) ? page : pageLoader(p);
    if (!pg) continue;

    const uint16_t fromWord = (p == startPage) ? startWord : 0;
    const uint16_t toWord = (p == endPage) ? endWord : UINT16_MAX;

    uint16_t idx = 0;
    for (const auto& element : pg->elements) {
      if (!element || element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = line.getBlock();
      if (!block) continue;

      const auto& wordList = block->getWords();
      const auto& xPositions = block->getWordXpos();
      const size_t count = std::min(wordList.size(), xPositions.size());
      for (size_t i = 0; i < count; ++i) {
        if (idx >= fromWord && idx <= toWord) {
          if (!snippet.empty()) snippet.push_back(' ');
          snippet += wordList[i];
          if (snippet.size() >= SNIPPET_BUILD_LIMIT) return snippet.substr(0, SNIPPET_BUILD_LIMIT);
        }
        ++idx;
      }
    }

    if (p == endPage) break;
  }

  return snippet;
}

void HighlightSelectActivity::saveHighlight(const uint16_t selectedPage, const uint16_t selectedWord) {
  uint16_t startPage = anchorPage;
  uint16_t startWord = anchorWord;
  uint16_t endPage = selectedPage;
  uint16_t endWord = selectedWord;
  if (startPage > endPage || (startPage == endPage && startWord > endWord)) {
    std::swap(startPage, endPage);
    std::swap(startWord, endWord);
  }

  const std::string snippet = buildSnippet(startPage, startWord, endPage, endWord);

  freeSelectionRegionCache();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }

  if (onSave) {
    onSave(startPage, startWord, endPage, endWord, snippet);
  }

  GUI.drawPopup(renderer, tr(STR_HIGHLIGHT_SAVED));
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  delay(700);

  setResult(ActivityResult{});
  finish();
}

void HighlightSelectActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (phase == Phase::PickEnd) {
      phase = Phase::PickStart;
      goToPage(anchorPage, &anchorWord, false);
      return;
    }
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (rows.empty()) return;
    const int wordIndex = rows[currentRow].wordIndices[currentWordInRow];
    const uint16_t selectedWordIndex = words[wordIndex].pageWordIndex;

    if (phase == Phase::PickStart) {
      anchorPage = currentPageNumber;
      anchorWord = selectedWordIndex;
      phase = Phase::PickEnd;
      invalidateSelectionRegionCache();
      requestUpdate();
      return;
    }

    saveHighlight(currentPageNumber, selectedWordIndex);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    moveRow(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    moveRow(1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    moveWord(-1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    moveWord(1);
  }
}

void HighlightSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();
  std::optional<FontCacheManager::PrewarmScope> fontPrewarm;
  if (page) {
    if (auto* fcm = renderer.getFontCacheManager()) {
      fontPrewarm.emplace(*fcm);
      page->recordFontUsage(*fcm, readerFontId, SETTINGS.bionicReading);
      fontPrewarm->endScanAndPrewarm();
    }
    page->render(renderer, readerFontId, marginLeft, marginTop, SETTINGS.bionicReading);
  }

  if (rows.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_LOOKUP_EMPTY_PAGE));
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sideBackgroundWidth = metrics.sideButtonHintsWidth + 8;
  const int sideBackgroundHeight = 168;
  if (gpio.deviceIsX3()) {
    constexpr int sideY = 151;
    renderer.fillRect(0, sideY, sideBackgroundWidth, sideBackgroundHeight / 2, false);
    renderer.fillRect(renderer.getScreenWidth() - sideBackgroundWidth, sideY, sideBackgroundWidth,
                      sideBackgroundHeight / 2, false);
  } else {
    const int sideY = std::min(341, std::max(0, renderer.getScreenHeight() - sideBackgroundHeight - 4));
    renderer.fillRect(renderer.getScreenWidth() - sideBackgroundWidth, sideY, sideBackgroundWidth,
                      sideBackgroundHeight, false);
  }

  const char* selectLabel = phase == Phase::PickStart ? tr(STR_SELECT_START_WORD) : tr(STR_SELECT_END_WORD);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), selectLabel, tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  storeSelectionBaseRegions();
  prewarmCurrentSelectionText();
  drawSelectionHighlight();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
