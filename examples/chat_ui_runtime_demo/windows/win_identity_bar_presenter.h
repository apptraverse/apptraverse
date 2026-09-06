#ifndef CHAT_WIN_IDENTITY_BAR_PRESENTER_H_
#define CHAT_WIN_IDENTITY_BAR_PRESENTER_H_

#include <functional>
#include <string>

#include "chat_identity_bar.h"
#include "chat_ids.h"
#include "chat_log.h"
#include "chat_model.h"
#include "win_util.h"

namespace chat::win32 {

// One status/UID field plus a role-specific button. No separate Registered
// label. Join room is intentionally unimplemented in this milestone.
class WinIdentityBarPresenter {
 public:
  static constexpr int kIdUid = 108;
  static constexpr int kIdCopy = 109;
  static constexpr int kIdJoin = 110;

  std::function<void()> on_join_room;

  void Create(HWND parent, HINSTANCE inst, ChatRole role) {
    parent_ = parent;
    role_ = role;
    int const label_w = kChatConnectionBarLabelWidth;
    int const btn_w = kChatConnectionBarButtonWidth;
    label_hwnd_ = CreateWindowExW(
        0, L"STATIC", L"Aether ID:", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0,
        label_w, 20, parent, nullptr, inst, nullptr);
    uid_hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"Registering in network...",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT | ES_READONLY, 0, 0, 0,
        0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdUid)), inst,
        nullptr);
    if (role_ == ChatRole::Host) {
      copy_hwnd_ = CreateWindowExW(
          0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
          btn_w, 24, parent,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCopy)), inst,
          nullptr);
      EnableWindow(copy_hwnd_, FALSE);
    } else {
      join_hwnd_ = CreateWindowExW(
          0, L"BUTTON", L"Join room", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,
          0, btn_w, 24, parent,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdJoin)), inst,
          nullptr);
      EnableWindow(join_hwnd_, FALSE);
    }
  }

  void Destroy() {
    label_hwnd_ = nullptr;
    uid_hwnd_ = nullptr;
    copy_hwnd_ = nullptr;
    join_hwnd_ = nullptr;
    parent_ = nullptr;
    network_ = {};
    aether_ = {};
  }

  HWND UidHwnd() const { return uid_hwnd_; }
  HWND CopyHwnd() const { return copy_hwnd_; }
  HWND JoinHwnd() const { return join_hwnd_; }
  HWND StatusHwnd() const { return nullptr; }

  void Layout(int client_w, int top, int height) {
    if (parent_ == nullptr || label_hwnd_ == nullptr || uid_hwnd_ == nullptr) {
      return;
    }
    HWND button = copy_hwnd_ != nullptr ? copy_hwnd_ : join_hwnd_;
    if (button == nullptr) {
      return;
    }
    int const gap = 8;
    int const label_w = kChatConnectionBarLabelWidth;
    int const btn_w = kChatConnectionBarButtonWidth;
    int const field_x = gap + label_w + gap;
    int const field_w = client_w - field_x - btn_w - gap * 2;
    if (field_w < 40) {
      return;
    }
    int const control_h = 24;
    int const y = top + (height - control_h) / 2;
    MoveWindow(label_hwnd_, gap, y + 2, label_w, 20, TRUE);
    MoveWindow(uid_hwnd_, field_x, y, field_w, control_h, TRUE);
    MoveWindow(button, field_x + field_w + gap, y, btn_w, control_h, TRUE);
  }

  void UpdateFromDomain(NetworkState::ptr network,
                        AetherRegistrationState::ptr aether, ChatRole role) {
    network_ = network;
    aether_ = aether;
    role_ = role;
    if (uid_hwnd_ == nullptr || !network.is_valid() || !aether.is_valid()) {
      return;
    }
    network.Load();
    aether.Load();
    auto view = ProjectIdentityBar(role_, *network, *aether);
    ApplyView(view);
  }

  bool HandleCommand(WPARAM wparam) {
    if (HIWORD(wparam) != BN_CLICKED) {
      return false;
    }
    auto const id = LOWORD(wparam);
    if (id == static_cast<WORD>(kIdCopy)) {
      CopyUidToClipboard();
      return true;
    }
    if (id == static_cast<WORD>(kIdJoin)) {
      ChatLog("JOIN_ROOM unimplemented");
      if (on_join_room) {
        on_join_room();
      }
      return true;
    }
    return false;
  }

 private:
  static constexpr UINT kEmSetCueBanner = 0x1501;

  void ApplyView(IdentityBarView const& view) {
    LONG style = GetWindowLongW(uid_hwnd_, GWL_STYLE);
    if (view.field_readonly) {
      style |= ES_READONLY;
    } else {
      style &= ~ES_READONLY;
    }
    SetWindowLongW(uid_hwnd_, GWL_STYLE, style);
    SendMessageW(uid_hwnd_, EM_SETREADONLY, view.field_readonly ? TRUE : FALSE,
                 0);

    auto wide = Utf8ToWide(view.field_text);
    int const len = GetWindowTextLengthW(uid_hwnd_);
    std::wstring current(static_cast<std::size_t>(len) + 1, L'\0');
    if (len > 0) {
      GetWindowTextW(uid_hwnd_, current.data(), len + 1);
    }
    current.resize(static_cast<std::size_t>(len));
    if (current != wide) {
      SetWindowTextW(uid_hwnd_, wide.c_str());
    }

    wchar_t const* cue =
        view.show_edit_cue ? kIdentityBarEnterRoomCue : L"";
    SendMessageW(uid_hwnd_, kEmSetCueBanner, TRUE,
                 reinterpret_cast<LPARAM>(cue));

    if (copy_hwnd_ != nullptr) {
      EnableWindow(copy_hwnd_, view.copy_enabled ? TRUE : FALSE);
    }
    if (join_hwnd_ != nullptr) {
      EnableWindow(join_hwnd_, view.join_enabled ? TRUE : FALSE);
    }
  }

  void CopyUidToClipboard() {
    if (!aether_.is_valid() || !aether_->IsRegisteredForCurrentRun()) {
      return;
    }
    auto wide = Utf8ToWide(aether_->CurrentUid());
    if (wide.empty()) {
      return;
    }
    CopyWideTextToClipboard(parent_, wide);
  }

  HWND parent_{nullptr};
  HWND label_hwnd_{nullptr};
  HWND uid_hwnd_{nullptr};
  HWND copy_hwnd_{nullptr};
  HWND join_hwnd_{nullptr};
  ChatRole role_{ChatRole::Host};
  NetworkState::ptr network_;
  AetherRegistrationState::ptr aether_;
};

}  // namespace chat::win32

#endif  // CHAT_WIN_IDENTITY_BAR_PRESENTER_H_
