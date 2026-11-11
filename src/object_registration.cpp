#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse {
void EnsureObjectRegistration() {
  EnsureCoreObjectRegistration();
  ui::EnsureUiPresenterRegistration();
}
}  // namespace apptraverse
