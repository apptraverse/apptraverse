#include "demo_model.h"

#include <cassert>

#include "demo_events.h"

namespace apptraverse {
namespace {

std::uint32_t NextDemoColor(std::uint32_t color) {
  switch (color) {
    case 0x00C04040:
      return 0x0040C040;
    case 0x0040C040:
      return 0x004040C0;
    case 0x004040C0:
      return 0x00C0A020;
    default:
      return 0x00C04040;
  }
}

}  // namespace

void ColorToolbar::Apply(ColorChangedEvent const& event) {
  if (color == event.color) {
    return;
  }
  color = event.color;
  NoteMaterializedChange();
}

void ColorToolbar::Update(std::chrono::steady_clock::time_point now) {
  if (last_color_tick_.time_since_epoch().count() == 0) {
    last_color_tick_ = now;
    return;
  }
  if (now - last_color_tick_ < demo::kColorChangePeriod) {
    return;
  }
  last_color_tick_ = now;
  auto event = ColorChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->color = NextDemoColor(color);
  Commit(event);
}

void Window::Apply(WindowBoundsChangedEvent const& event) {
  std::int32_t next_left = event.left;
  std::int32_t next_top = event.top;
  std::int32_t next_right = event.right;
  std::int32_t next_bottom = event.bottom;
  if (next_right <= next_left) {
    next_right = next_left + 1;
  }
  if (next_bottom <= next_top) {
    next_bottom = next_top + 1;
  }
  std::int32_t next_client_w = event.client_width;
  std::int32_t next_client_h = event.client_height;
  if (next_client_w < 1) {
    next_client_w = 1;
  }
  if (next_client_h < 1) {
    next_client_h = 1;
  }

  bool const changed = left != next_left || top != next_top ||
                       right != next_right || bottom != next_bottom ||
                       client_width != next_client_w ||
                       client_height != next_client_h;
  left = next_left;
  top = next_top;
  right = next_right;
  bottom = next_bottom;
  client_width = next_client_w;
  client_height = next_client_h;
  if (changed) {
    NoteMaterializedChange();
  }
}

}  // namespace apptraverse
