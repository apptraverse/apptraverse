#include "surfaces_demo_language_button.h"

namespace apptraverse::example::surfaces {

void SurfacesDemoLanguageButton::HandlePushButtonClicked() {
  if (!locale) {
    return;
  }
  int const next = 1 - locale->language_index();
  locale->SubmitLanguageIndexFromUi(next);
}

}  // namespace apptraverse::example::surfaces
