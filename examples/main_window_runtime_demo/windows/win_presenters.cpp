#include "win_presenters.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Win32MainWindowPresenter);

}  // namespace

void EnsureWin32MainWindowPresenterRegistration() {
  EnsureObjectRegistration();
}

}  // namespace apptraverse
