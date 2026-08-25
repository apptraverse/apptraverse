#include "demo_model.h"

#include <cassert>

#include "aether-miscpp/serialization/binary_archive.h"

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

void TextToolbar::UpdateFromParent(Window const& window) {
  std::int32_t const next_width = window.client_width;
  std::int32_t const next_height = demo::kTextToolbarHeight;
  std::int32_t const next_x = 0;
  std::int32_t const next_y = 0;
  if (x == next_x && y == next_y && width == next_width &&
      height == next_height) {
    return;
  }
  x = next_x;
  y = next_y;
  width = next_width;
  height = next_height;
  NoteMaterializedChange();
}

void TextToolbar::WriteUiState(void const* model, ByteSink& out) {
  auto const& bar = *static_cast<TextToolbar const*>(model);
  auto archive = UiArchive(out.bytes);
  archive.Save(bar.x);
  archive.Save(bar.y);
  archive.Save(bar.width);
  archive.Save(bar.height);
  archive.Save(bar.text_id.id());
}

void ColorToolbar::Apply(ColorChangedEvent const& event) {
  if (color == event.color) {
    return;
  }
  color = event.color;
  NoteMaterializedChange();
}

void ColorToolbar::UpdateFromParent(Window const& window) {
  std::int32_t const next_width = window.client_width;
  std::int32_t const next_height = demo::kColorToolbarHeight;
  std::int32_t const next_x = 0;
  std::int32_t const next_y = demo::kTextToolbarHeight;
  if (x == next_x && y == next_y && width == next_width &&
      height == next_height) {
    return;
  }
  x = next_x;
  y = next_y;
  width = next_width;
  height = next_height;
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
  assert(domain != nullptr);
  auto event = ColorChangedEvent::ptr::Create(ae::CreateWith{*domain});
  event->color = NextDemoColor(color);
  Commit(event);
}

void ColorToolbar::WriteUiState(void const* model, ByteSink& out) {
  auto const& bar = *static_cast<ColorToolbar const*>(model);
  auto archive = UiArchive(out.bytes);
  archive.Save(bar.x);
  archive.Save(bar.y);
  archive.Save(bar.width);
  archive.Save(bar.height);
  archive.Save(bar.color);
}

void Chat::Apply(AddMessageEvent const& event) {
  messages.push_back(event.text);
  NoteMaterializedChange();
}

void Chat::UpdateFromParent(Window const& window) {
  std::int32_t const toolbar_h =
      demo::kTextToolbarHeight + demo::kColorToolbarHeight;
  std::int32_t next_width = (window.client_width * 2) / 3;
  if (next_width < 1) {
    next_width = 1;
  }
  std::int32_t next_x = (window.client_width - next_width) / 2;
  if (next_x < 0) {
    next_x = 0;
  }
  std::int32_t next_y = toolbar_h;
  std::int32_t next_height = window.client_height - toolbar_h;
  if (next_height < 1) {
    next_height = 1;
  }
  if (x == next_x && y == next_y && width == next_width &&
      height == next_height) {
    return;
  }
  x = next_x;
  y = next_y;
  width = next_width;
  height = next_height;
  NoteMaterializedChange();
}

void Chat::WriteUiState(void const* model, ByteSink& out) {
  auto const& chat = *static_cast<Chat const*>(model);
  auto archive = UiArchive(out.bytes);
  archive.Save(chat.x);
  archive.Save(chat.y);
  archive.Save(chat.width);
  archive.Save(chat.height);
  archive.Save(chat.messages);
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
                       dpi != event.dpi || client_width != next_client_w ||
                       client_height != next_client_h;
  left = next_left;
  top = next_top;
  right = next_right;
  bottom = next_bottom;
  dpi = event.dpi;
  client_width = next_client_w;
  client_height = next_client_h;
  if (changed) {
    NoteMaterializedChange();
  }

  if (text_toolbar) {
    text_toolbar.Load();
    text_toolbar->EnsureCurrentGeneration();
    text_toolbar->UpdateFromParent(*this);
  }
  if (color_toolbar) {
    color_toolbar.Load();
    color_toolbar->EnsureCurrentGeneration();
    color_toolbar->UpdateFromParent(*this);
  }
  if (chat) {
    chat.Load();
    chat->EnsureCurrentGeneration();
    chat->UpdateFromParent(*this);
  }
}

void Window::WriteUiState(void const* model, ByteSink& out) {
  auto const& window = *static_cast<Window const*>(model);
  auto archive = UiArchive(out.bytes);
  archive.Save(window.left);
  archive.Save(window.top);
  archive.Save(window.right);
  archive.Save(window.bottom);
  archive.Save(window.dpi);
  archive.Save(window.client_width);
  archive.Save(window.client_height);
}

}  // namespace apptraverse
