#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "../reader/HighlightStore.h"
#include "util/ButtonNavigator.h"

class HighlightsAppActivity final : public Activity {
  struct BookEntry {
    std::string bookId;
    std::string path;
    std::string title;
    std::string author;
    std::vector<HighlightStore::Highlight> highlights;
  };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::vector<BookEntry> entries;

  void refreshEntries();
  void openSelectedBook();
  bool clearHighlightsForBook(const std::string& bookId) const;
  void confirmDeleteSelectedBook();

 public:
  explicit HighlightsAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HighlightsApp", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
