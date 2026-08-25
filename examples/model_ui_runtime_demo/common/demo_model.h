#ifndef APPTRAVERSE_DEMO_MODEL_H_
#define APPTRAVERSE_DEMO_MODEL_H_

#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "aether/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui_subgraph.h"

#include "demo_ids.h"

namespace apptraverse {

class WindowBoundsChangedEvent;
class ColorChangedEvent;
class AddMessageEvent;

class ImmutableString : public ae::Obj {
  APPTRAVERSE_OBJECT(ImmutableString, ae::Obj, 0)

 protected:
  ImmutableString() = default;

 public:
  explicit ImmutableString(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(bytes))

  std::string bytes;
};

template <typename T>
struct ConstRef {
  ae::ObjId id;
  T const* ptr{nullptr};

  T const* get() const {
    assert(ptr != nullptr);
    return ptr;
  }
};

class Window;
class TextToolbar;
class ColorToolbar;
class Chat;

class TextToolbar : public NodeFor<TextToolbar> {
  APPTRAVERSE_OBJECT(TextToolbar, Node, 0)

 protected:
  TextToolbar() = default;

 public:
  explicit TextToolbar(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(text_id))

  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{demo::kTextToolbarHeight};
  ae::ObjId text_id;

  ConstRef<ImmutableString> text;

  void UpdateFromParent(Window const& window);
  static void WriteUiState(void const* model, ByteSink& out);
};

class ColorToolbar : public NodeFor<ColorToolbar> {
  APPTRAVERSE_OBJECT(ColorToolbar, Node, 0)

 protected:
  ColorToolbar() = default;

 public:
  explicit ColorToolbar(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(color))

  std::int32_t x{0};
  std::int32_t y{demo::kTextToolbarHeight};
  std::int32_t width{0};
  std::int32_t height{demo::kColorToolbarHeight};
  std::uint32_t color{0x00C04040};

  void Apply(ColorChangedEvent const& event);
  void UpdateFromParent(Window const& window);
  void Update(std::chrono::steady_clock::time_point now) override;
  static void WriteUiState(void const* model, ByteSink& out);

 private:
  std::chrono::steady_clock::time_point last_color_tick_{};
};

class Chat : public NodeFor<Chat> {
  APPTRAVERSE_OBJECT(Chat, Node, 0)

 protected:
  Chat() = default;

 public:
  explicit Chat(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(messages))

  std::int32_t x{0};
  std::int32_t y{demo::kTextToolbarHeight + demo::kColorToolbarHeight};
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<std::string> messages;

  void Apply(AddMessageEvent const& event);
  void UpdateFromParent(Window const& window);
  static void WriteUiState(void const* model, ByteSink& out);
};

class Window : public NodeFor<Window> {
  APPTRAVERSE_OBJECT(Window, Node, 0)

 protected:
  Window() = default;

 public:
  explicit Window(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(left), AE_MMBR(top), AE_MMBR(right),
                    AE_MMBR(bottom), AE_MMBR(dpi), AE_MMBR(client_width),
                    AE_MMBR(client_height), AE_MMBR(text_toolbar),
                    AE_MMBR(color_toolbar), AE_MMBR(chat))

  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t right{0};
  std::int32_t bottom{0};
  std::int32_t dpi{demo::kDefaultDpi};
  std::int32_t client_width{0};
  std::int32_t client_height{0};

  LocalPtr<TextToolbar> text_toolbar;
  LocalPtr<ColorToolbar> color_toolbar;
  LocalPtr<Chat> chat;

  void Apply(WindowBoundsChangedEvent const& event);
  static void WriteUiState(void const* model, ByteSink& out);
};

class Application : public ae::Obj {
  APPTRAVERSE_OBJECT(Application, ae::Obj, 0)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_a), AE_MMBR(window_b),
                    AE_MMBR(toolbar_text))

  LocalPtr<Window> window_a;
  LocalPtr<Window> window_b;
  LocalPtr<ImmutableString> toolbar_text;
};

void EnsureDemoRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_MODEL_H_
