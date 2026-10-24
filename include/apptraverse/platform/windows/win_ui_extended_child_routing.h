#pragma once

#include <cstdint>

namespace apptraverse {
class Presenter;
namespace ui::windows {

using ExtendedChildCommandRouter = bool (*)(std::uint16_t notification_code,
                                            Presenter* child_presenter);

void SetExtendedChildCommandRouter(ExtendedChildCommandRouter router);
bool TryRouteExtendedChildCommand(std::uint16_t notification_code,
                                  Presenter* child_presenter);

}  // namespace ui::windows
}  // namespace apptraverse
