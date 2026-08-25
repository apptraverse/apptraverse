#ifndef APPTRAVERSE_WIN_CHAT_PRESENTER_H_
#define APPTRAVERSE_WIN_CHAT_PRESENTER_H_

#include <functional>
#include <string>

#include "win_util.h"
#include "ui_runtime_registry.h"

namespace apptraverse {

class WinChatPresenter {
 public:
  using SendFn = std::function<void(std::string)>;

  void Create(HWND parent, RuntimeChat const& runtime, SendFn send) {
    runtime_ = &runtime;
    send_ = std::move(send);
    parent_ = parent;
    transcript_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)),
        GetModuleHandleW(nullptr), nullptr);
    edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)),
        GetModuleHandleW(nullptr), nullptr);
    send_btn_ = CreateWindowExW(
        0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0,
        0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(3)),
        GetModuleHandleW(nullptr), nullptr);
    ApplyBounds();
    ApplyMessages();
  }

  void Present(RuntimeChat const& runtime) {
    runtime_ = &runtime;
    if (runtime.generation == last_generation_) {
      return;
    }
    last_generation_ = runtime.generation;
    ApplyBounds();
    ApplyMessages();
  }

  void OnSendClicked() {
    if (edit_ == nullptr || !send_) {
      return;
    }
    int const len = GetWindowTextLengthW(edit_);
    std::wstring wide(static_cast<std::size_t>(len) + 1, L'\0');
    int const copied = GetWindowTextW(edit_, wide.data(), len + 1);
    wide.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
    auto text = WideToUtf8(wide);
    if (text.empty()) {
      return;
    }
    send_(std::move(text));
    SetWindowTextW(edit_, L"");
  }

 private:
  void ApplyBounds() {
    if (runtime_ == nullptr) {
      return;
    }
    int const x = runtime_->x;
    int const y = runtime_->y;
    int const w = runtime_->width;
    int const h = runtime_->height;
    int const edit_h = 28;
    int const send_w = 72;
    int const transcript_h = h - edit_h - 8;
    if (x != last_x_ || y != last_y_ || w != last_w_ || h != last_h_) {
      last_x_ = x;
      last_y_ = y;
      last_w_ = w;
      last_h_ = h;
      if (transcript_ != nullptr) {
        MoveWindow(transcript_, x, y, w, transcript_h > 0 ? transcript_h : 1,
                   TRUE);
      }
      if (edit_ != nullptr) {
        MoveWindow(edit_, x, y + h - edit_h, w - send_w - 8, edit_h, TRUE);
      }
      if (send_btn_ != nullptr) {
        MoveWindow(send_btn_, x + w - send_w, y + h - edit_h, send_w, edit_h,
                   TRUE);
      }
    }
  }

  void ApplyMessages() {
    if (runtime_ == nullptr || transcript_ == nullptr) {
      return;
    }
    if (runtime_->messages.size() == last_message_count_) {
      return;
    }
    last_message_count_ = runtime_->messages.size();
    std::string joined;
    for (auto const& line : runtime_->messages) {
      joined += line;
      joined += "\r\n";
    }
    SetWindowTextW(transcript_, Utf8ToWide(joined).c_str());
  }

  HWND parent_{nullptr};
  HWND transcript_{nullptr};
  HWND edit_{nullptr};
  HWND send_btn_{nullptr};
  RuntimeChat const* runtime_{nullptr};
  SendFn send_;
  std::uint64_t last_generation_{0};
  std::size_t last_message_count_{0};
  std::int32_t last_x_{0};
  std::int32_t last_y_{0};
  std::int32_t last_w_{0};
  std::int32_t last_h_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_CHAT_PRESENTER_H_
