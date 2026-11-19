#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_text_control.h"

namespace apptraverse::ui {

void EnsureUiControlRegistration() {
  EnsureUiWindowControlRegistration();
  EnsureUiTextControlRegistration();
}

}  // namespace apptraverse::ui
