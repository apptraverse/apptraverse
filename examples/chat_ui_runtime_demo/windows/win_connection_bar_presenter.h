#ifndef APPTRAVERSE_CHAT_WIN_CONNECTION_BAR_PRESENTER_H_
#define APPTRAVERSE_CHAT_WIN_CONNECTION_BAR_PRESENTER_H_

#include <functional>
#include <string>

#include "chat_ids.h"
#include "chat_model.h"
#include "win_util.h"

namespace apptraverse {

class WinConnectionBarPresenter {
 public:
  static constexpr int kIdUid = 108;
  static constexpr int kIdAction = 109;

  std::function<void(std::string)> on_connect_host;

  void Create(HWND parent, ChatRole role, HINSTANCE inst) {
    parent_ = parent;
    role_ = role;
    int const label_w = chat::kChatConnectionBarLabelWidth;
    int const btn_w = chat::kChatConnectionBarButtonWidth;
    if (role == ChatRole::Host) {
      label_hwnd_ = CreateWindowExW(
          0, L"STATIC", L"Your Aether ID:", WS_CHILD | WS_VISIBLE | SS_LEFT, 0,
          0, label_w, 20, parent, nullptr, inst, nullptr);
      uid_hwnd_ = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"...",
          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT | ES_READONLY, 0, 0,
          0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdUid)),
          inst, nullptr);
      action_hwnd_ = CreateWindowExW(
          0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
          btn_w, 24, parent,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAction)), inst,
          nullptr);
    } else {
      label_hwnd_ = CreateWindowExW(
          0, L"STATIC", L"Host Aether ID:", WS_CHILD | WS_VISIBLE | SS_LEFT, 0,
          0, label_w, 20, parent, nullptr, inst, nullptr);
      uid_hwnd_ = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0, 0, parent,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdUid)), inst, nullptr);
      action_hwnd_ = CreateWindowExW(
          0, L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,
          0, btn_w, 24, parent,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAction)), inst,
          nullptr);
    }
  }

  void Destroy() {
    label_hwnd_ = nullptr;
    uid_hwnd_ = nullptr;
    action_hwnd_ = nullptr;
    parent_ = nullptr;
    identity_ = LocalAetherIdentity::ptr{};
  }

  void Layout(int client_w, int top, int height) {
    if (parent_ == nullptr || label_hwnd_ == nullptr ||
        uid_hwnd_ == nullptr || action_hwnd_ == nullptr) {
      return;
    }
    int const gap = 8;
    int const label_w = chat::kChatConnectionBarLabelWidth;
    int const btn_w = chat::kChatConnectionBarButtonWidth;
    int const field_x = gap + label_w + gap;
    int const field_w = client_w - field_x - btn_w - gap * 2;
    if (field_w < 40) {
      return;
    }
    int const control_h = 24;
    int const y = top + (height - control_h) / 2;
    MoveWindow(label_hwnd_, gap, y + 2, label_w, 20, TRUE);
    MoveWindow(uid_hwnd_, field_x, y, field_w, control_h, TRUE);
    MoveWindow(action_hwnd_, field_x + field_w + gap, y, btn_w, control_h,
               TRUE);
  }

  void UpdateFromDomain(LocalAetherIdentity::ptr identity) {
    identity_ = identity;
    if (role_ != ChatRole::Host || uid_hwnd_ == nullptr ||
        !identity.is_valid()) {
      return;
    }
    auto wide = Utf8ToWide(identity->UidTextBytes());
    SetWindowTextW(uid_hwnd_, wide.c_str());
  }

  bool HandleCommand(WPARAM wparam) {
    if (HIWORD(wparam) != BN_CLICKED) {
      return false;
    }
    if (LOWORD(wparam) != static_cast<WORD>(kIdAction)) {
      return false;
    }
    if (role_ == ChatRole::Host) {
      CopyUidToClipboard();
    } else {
      TryConnectHost();
    }
    return true;
  }

  ChatRole role() const { return role_; }

 private:
  void CopyUidToClipboard() {
    if (!identity_.is_valid()) {
      return;
    }
    auto wide = Utf8ToWide(identity_->UidTextBytes());
    if (wide.empty()) {
      return;
    }
    CopyWideTextToClipboard(parent_, wide);
  }

  void TryConnectHost() {
    if (!on_connect_host || uid_hwnd_ == nullptr) {
      return;
    }
    auto text = ReadEditTextUtf8(uid_hwnd_);
    if (text.empty()) {
      return;
    }
    on_connect_host(std::move(text));
  }

  ChatRole role_{ChatRole::Host};
  HWND parent_{nullptr};
  HWND label_hwnd_{nullptr};
  HWND uid_hwnd_{nullptr};
  HWND action_hwnd_{nullptr};
  LocalAetherIdentity::ptr identity_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_WIN_CONNECTION_BAR_PRESENTER_H_
