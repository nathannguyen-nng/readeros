#pragma once
#include <string>
#include <vector>

#include "../../fontIds.h"
#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Small reusable dialog: heading + body text, then a vertical list of options.
// Confirm returns the selected index via MenuResult{selectedIndex}; Back cancels.
class OptionDialogActivity final : public Activity {
 private:
  std::string heading;
  std::string body;
  std::vector<const char*> options;

  const int margin = 20;
  const int spacing = 30;
  const int fontId = UI_10_FONT_ID;

  std::string safeHeading;
  std::string safeBody;
  int startY = 0;
  int lineHeight = 0;
  int optionsTop = 0;

  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

 public:
  OptionDialogActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string heading, std::string body,
                       std::vector<const char*> options);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
