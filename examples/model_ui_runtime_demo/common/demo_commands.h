#ifndef APPTRAVERSE_DEMO_COMMANDS_H_
#define APPTRAVERSE_DEMO_COMMANDS_H_

#include <cassert>
#include <cstdint>

#include "demo_events.h"
#include "demo_model.h"

namespace apptraverse {

struct WindowBoundsCommand {
  std::uint32_t window_id{0};
  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t right{0};
  std::int32_t bottom{0};
  std::int32_t client_width{0};
  std::int32_t client_height{0};
};

struct AdvanceToolbarTextCommand {
  std::uint32_t toolbar_id{0};
};

inline bool BoundsMatchWindow(Window const& window,
                              WindowBoundsCommand const& command) {
  return window.left == command.left && window.top == command.top &&
         window.right == command.right && window.bottom == command.bottom &&
         window.client_width == command.client_width &&
         window.client_height == command.client_height;
}

inline void CommitWindowBounds(Window& window,
                               WindowBoundsCommand const& command) {
  if (BoundsMatchWindow(window, command)) {
    return;
  }
  auto event =
      WindowBoundsChangedEvent::ptr::Create(ae::CreateWith{*window.domain});
  event->left = command.left;
  event->top = command.top;
  event->right = command.right;
  event->bottom = command.bottom;
  event->client_width = command.client_width;
  event->client_height = command.client_height;
  window.Commit(event);
}

inline void CommitAdvanceToolbarText(TextToolbar& toolbar) {
  assert(toolbar.text.is_valid());
  toolbar.text.Load();
  assert(toolbar.text.is_loaded());
  auto neu = ImmutableString::ptr::Create(ae::CreateWith{*toolbar.domain});
  neu->bytes = toolbar.text->bytes + " *";
  auto event =
      TextReplacedEvent::ptr::Create(ae::CreateWith{*toolbar.domain});
  event->text = neu;
  toolbar.Commit(event);
}

inline Window* WindowById(Application& app, std::uint32_t id) {
  if (app.window_a.id().id() == id) {
    return &*app.window_a;
  }
  return &*app.window_b;
}

inline TextToolbar* TextToolbarById(Application& app, std::uint32_t id) {
  if (app.window_b->text_toolbar.id().id() == id) {
    return &*app.window_b->text_toolbar;
  }
  return nullptr;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_COMMANDS_H_
