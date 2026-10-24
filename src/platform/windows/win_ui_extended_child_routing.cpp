#include "apptraverse/platform/windows/win_ui_extended_child_routing.h"

#include "apptraverse/presenter.h"

namespace apptraverse::ui::windows {
namespace {

ExtendedChildCommandRouter g_extended_child_router = nullptr;

}  // namespace

void SetExtendedChildCommandRouter(ExtendedChildCommandRouter router) {
  g_extended_child_router = router;
}

bool TryRouteExtendedChildCommand(std::uint16_t notification_code,
                                  Presenter* child_presenter) {
  if (g_extended_child_router == nullptr) {
    return false;
  }
  return g_extended_child_router(notification_code, child_presenter);
}

}  // namespace apptraverse::ui::windows
