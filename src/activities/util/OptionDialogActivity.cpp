#include "OptionDialogActivity.h"

#include <I18n.h>

#include "../../components/UITheme.h"
#include "HalDisplay.h"

namespace {
constexpr int OPTION_ROW_HEIGHT = 30;
}

OptionDialogActivity::OptionDialogActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::string heading, std::string body, std::vector<const char*> options)
    : Activity("OptionDialog", renderer, mappedInput),
      heading(std::move(heading)),
      body(std::move(body)),
      options(std::move(options)) {}

void OptionDialogActivity::onEnter() {
  Activity::onEnter();

  lineHeight = renderer.getLineHeight(fontId);
  const int maxWidth = renderer.getScreenWidth() - (margin * 2);

  if (!heading.empty()) {
    safeHeading = renderer.truncatedText(fontId, heading.c_str(), maxWidth, EpdFontFamily::BOLD);
  }
  if (!body.empty()) {
    safeBody = renderer.truncatedText(fontId, body.c_str(), maxWidth, EpdFontFamily::REGULAR);
  }

  int textHeight = 0;
  if (!safeHeading.empty()) textHeight += lineHeight;
  if (!safeBody.empty()) textHeight += lineHeight;
  if (!safeHeading.empty() && !safeBody.empty()) textHeight += spacing;

  const int optionsHeight = static_cast<int>(options.size()) * OPTION_ROW_HEIGHT;
  const int totalHeight = textHeight + (textHeight > 0 ? spacing : 0) + optionsHeight;

  startY = (renderer.getScreenHeight() - totalHeight) / 2;
  optionsTop = startY + textHeight + (textHeight > 0 ? spacing : 0);

  selectedIndex = 0;
  requestUpdate(true);
}

void OptionDialogActivity::render(RenderLock&&) {
  renderer.clearScreen();

  int currentY = startY;
  if (!safeHeading.empty()) {
    renderer.drawCenteredText(fontId, currentY, safeHeading.c_str(), true, EpdFontFamily::BOLD);
    currentY += lineHeight + spacing;
  }
  if (!safeBody.empty()) {
    renderer.drawCenteredText(fontId, currentY, safeBody.c_str(), true, EpdFontFamily::REGULAR);
  }

  const int pageWidth = renderer.getScreenWidth();
  for (size_t i = 0; i < options.size(); ++i) {
    const int rowY = optionsTop + static_cast<int>(i) * OPTION_ROW_HEIGHT;
    const bool isSelected = static_cast<int>(i) == selectedIndex;
    if (isSelected) {
      renderer.fillRect(margin, rowY - 2, pageWidth - margin * 2, OPTION_ROW_HEIGHT);
    }
    const std::string label = renderer.truncatedText(fontId, options[i], pageWidth - margin * 2);
    const int labelX = (pageWidth - renderer.getTextWidth(fontId, label.c_str())) / 2;
    renderer.drawText(fontId, labelX, rowY, label.c_str(), !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void OptionDialogActivity::loop() {
  const int totalOptions = static_cast<int>(options.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ActivityResult res;
    res.data = MenuResult{selectedIndex};
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  buttonNavigator.onNextRelease([this, totalOptions] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, totalOptions);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalOptions] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, totalOptions);
    requestUpdate();
  });
}
