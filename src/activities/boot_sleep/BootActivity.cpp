#include "BootActivity.h"

#include <GfxRenderer.h>

#include "fontIds.h"
#include "images/InkWyrmLogo.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int logoX = (pageWidth - INKWYRM_LOGO_WIDTH) / 2;
  const int logoY = (pageHeight - INKWYRM_LOGO_HEIGHT) / 2;

  renderer.clearScreen();
  renderer.drawIcon(InkWyrmLogo, logoX, logoY, INKWYRM_LOGO_WIDTH, INKWYRM_LOGO_HEIGHT);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, INKWYRM_VERSION);
  // White-on-black boot branding via full-buffer invert (same primitive the
  // sleep screen uses); FAST_REFRESH avoids the full-refresh white flash.
  renderer.invertScreen();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
