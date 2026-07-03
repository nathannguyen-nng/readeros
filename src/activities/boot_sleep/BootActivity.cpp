#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"

namespace {
constexpr int SUBTITLE_GAP = 25;
}

void BootActivity::onEnter() {
  Activity::onEnter();
  const bool restoreDarkMode = renderer.isDarkMode();
  if (restoreDarkMode) {
    renderer.setDarkMode(false);
  }

  const auto pageHeight = renderer.getScreenHeight();
  const int titleY = pageHeight / 2 - 20;
  const int subtitleY = titleY + SUBTITLE_GAP;

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, titleY, tr(STR_CPR_VCODEX), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, subtitleY, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();

  if (restoreDarkMode) {
    renderer.setDarkMode(true);
  }
}
