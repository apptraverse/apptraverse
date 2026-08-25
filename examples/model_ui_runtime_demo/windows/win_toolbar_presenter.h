#ifndef APPTRAVERSE_WIN_TOOLBAR_PRESENTER_H_
#define APPTRAVERSE_WIN_TOOLBAR_PRESENTER_H_

#include <cassert>

#include "win_util.h"

#include "immutable_object_store.h"
#include "ui_runtime_registry.h"

namespace apptraverse {

class WinTextToolbarPresenter {
 public:
  void Create(HWND parent, RuntimeTextToolbar const& runtime,
              ImmutableObjectStore const& store) {
    runtime_ = &runtime;
    store_ = &store;
    hwnd_ = CreateWindowExW(0, L"STATIC", L"",
                            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0,
                            0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr),
                            nullptr);
    ApplyText();
    ApplyBounds();
  }

  void Present(RuntimeTextToolbar const& runtime) {
    runtime_ = &runtime;
    if (runtime.generation == last_generation_) {
      return;
    }
    last_generation_ = runtime.generation;
    ApplyBounds();
    if (runtime.text_id != last_text_id_) {
      ApplyText();
    }
  }

  HWND hwnd() const { return hwnd_; }

 private:
  void ApplyBounds() {
    if (runtime_ == nullptr || hwnd_ == nullptr) {
      return;
    }
    if (runtime_->x == last_x_ && runtime_->y == last_y_ &&
        runtime_->width == last_w_ && runtime_->height == last_h_) {
      return;
    }
    last_x_ = runtime_->x;
    last_y_ = runtime_->y;
    last_w_ = runtime_->width;
    last_h_ = runtime_->height;
    MoveWindow(hwnd_, last_x_, last_y_, last_w_, last_h_, TRUE);
  }

  void ApplyText() {
    if (runtime_ == nullptr || store_ == nullptr || hwnd_ == nullptr) {
      return;
    }
    last_text_id_ = runtime_->text_id;
    auto const* text = store_->Find(runtime_->text_id);
    assert(text != nullptr);
    SetWindowTextW(hwnd_, Utf8ToWide(text->bytes).c_str());
  }

  HWND hwnd_{nullptr};
  RuntimeTextToolbar const* runtime_{nullptr};
  ImmutableObjectStore const* store_{nullptr};
  std::uint64_t last_generation_{0};
  ae::ObjId last_text_id_;
  std::int32_t last_x_{0};
  std::int32_t last_y_{0};
  std::int32_t last_w_{0};
  std::int32_t last_h_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_TOOLBAR_PRESENTER_H_
