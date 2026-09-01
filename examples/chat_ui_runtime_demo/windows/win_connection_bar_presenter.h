#ifndef APPTRAVERSE_CHAT_WIN_CONNECTION_BAR_PRESENTER_H_
#define APPTRAVERSE_CHAT_WIN_CONNECTION_BAR_PRESENTER_H_

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "chat_connection_ui_state.h"
#include "chat_ids.h"
#include "chat_model.h"
#include "win_util.h"

namespace apptraverse {

class WinConnectionBarPresenter {
 public:
  static constexpr int kIdUid = 108;
  static constexpr int kIdAction = 109;
  static constexpr UINT_PTR kConnectElapsedTimerId = 41;

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
      status_hwnd_ = CreateWindowExW(
          0, L"STATIC", L"Not connected", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0,
          0, 0, parent, nullptr, inst, nullptr);
      connection_ui_.status = ChatConnectionUiStatus::NotConnected;
      connection_ui_.connect_enabled = true;
    }
  }

  void Destroy() {
    StopConnectTimer();
    label_hwnd_ = nullptr;
    uid_hwnd_ = nullptr;
    action_hwnd_ = nullptr;
    status_hwnd_ = nullptr;
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
    int const status_w = (role_ == ChatRole::Client && status_hwnd_ != nullptr)
                             ? 200
                             : 0;
    int const field_x = gap + label_w + gap;
    int const field_w =
        client_w - field_x - btn_w - gap * 2 - (status_w > 0 ? status_w + gap : 0);
    if (field_w < 40) {
      return;
    }
    int const control_h = 24;
    int const y = top + (height - control_h) / 2;
    MoveWindow(label_hwnd_, gap, y + 2, label_w, 20, TRUE);
    MoveWindow(uid_hwnd_, field_x, y, field_w, control_h, TRUE);
    MoveWindow(action_hwnd_, field_x + field_w + gap, y, btn_w, control_h,
               TRUE);
    if (status_hwnd_ != nullptr && status_w > 0) {
      MoveWindow(status_hwnd_, field_x + field_w + gap + btn_w + gap, y + 2,
                 status_w, 20, TRUE);
    }
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

  void OnConnectTimer() {
    if (role_ != ChatRole::Client ||
        connection_ui_.status != ChatConnectionUiStatus::Connecting) {
      return;
    }
    RefreshConnectingElapsed();
  }

  // Called on UI thread when PeerReady / stream ready arrives.
  void NotifyPeerReady() {
    if (role_ != ChatRole::Client) {
      return;
    }
    double elapsed = 0.0;
    if (connect_t0_.has_value()) {
      elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                              *connect_t0_)
                    .count();
    }
    StopConnectTimer();
    connection_ui_.status = ChatConnectionUiStatus::Connected;
    connection_ui_.elapsed_sec = elapsed;
    connection_ui_.connect_enabled = true;
    ApplyConnectionUi();
  }

  void NotifyPeerDisconnected() {
    if (role_ != ChatRole::Client) {
      return;
    }
    StopConnectTimer();
    connection_ui_.status = ChatConnectionUiStatus::Disconnected;
    connection_ui_.elapsed_sec = 0.0;
    connection_ui_.connect_enabled = true;
    ApplyConnectionUi();
  }

  ChatRole role() const { return role_; }

  ChatConnectionUiState const& connection_ui() const { return connection_ui_; }

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

  static std::string TrimAscii(std::string text) {
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
            text.back() == '\t')) {
      text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() &&
           (text[start] == ' ' || text[start] == '\t')) {
      ++start;
    }
    if (start > 0) {
      text.erase(0, start);
    }
    return text;
  }

  void TryConnectHost() {
    if (!on_connect_host || uid_hwnd_ == nullptr) {
      return;
    }
    auto text = TrimAscii(ReadEditTextUtf8(uid_hwnd_));
    if (!LooksLikeAetherUid(text)) {
      StopConnectTimer();
      connection_ui_.status = ChatConnectionUiStatus::InvalidId;
      connection_ui_.elapsed_sec = 0.0;
      connection_ui_.connect_enabled = true;
      ApplyConnectionUi();
      return;
    }
    // Idempotent: same UID while already connected still invokes OpenPeer
    // (runtime no-ops duplicate stream) but does not restart elapsed unless
    // we were not Connected.
    bool const same_uid = text == last_connect_uid_;
    bool const already_connected =
        same_uid &&
        connection_ui_.status == ChatConnectionUiStatus::Connected;
    if (same_uid &&
        connection_ui_.status == ChatConnectionUiStatus::Connecting) {
      return;
    }
    last_connect_uid_ = text;
    if (!already_connected) {
      connect_t0_ = std::chrono::steady_clock::now();
      connection_ui_.status = ChatConnectionUiStatus::Connecting;
      connection_ui_.elapsed_sec = 0.0;
      // Disable Connect only during the initial OpenPeer request window.
      connection_ui_.connect_enabled = false;
      ApplyConnectionUi();
      StartConnectTimer();
    }
    on_connect_host(std::move(text));
    if (already_connected) {
      connection_ui_.connect_enabled = true;
      ApplyConnectionUi();
    }
  }

  void RefreshConnectingElapsed() {
    if (!connect_t0_.has_value()) {
      return;
    }
    connection_ui_.elapsed_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      *connect_t0_)
            .count();
    ApplyConnectionUi();
  }

  void ApplyConnectionUi() {
    if (status_hwnd_ != nullptr) {
      auto wide = Utf8ToWide(FormatConnectionStatusText(connection_ui_));
      SetWindowTextW(status_hwnd_, wide.c_str());
    }
    if (action_hwnd_ != nullptr && role_ == ChatRole::Client) {
      EnableWindow(action_hwnd_,
                   connection_ui_.connect_enabled ? TRUE : FALSE);
    }
  }

  void StartConnectTimer() {
    if (parent_ == nullptr) {
      return;
    }
    SetTimer(parent_, kConnectElapsedTimerId, 100, nullptr);
  }

  void StopConnectTimer() {
    if (parent_ != nullptr) {
      KillTimer(parent_, kConnectElapsedTimerId);
    }
  }

  ChatRole role_{ChatRole::Host};
  HWND parent_{nullptr};
  HWND label_hwnd_{nullptr};
  HWND uid_hwnd_{nullptr};
  HWND action_hwnd_{nullptr};
  HWND status_hwnd_{nullptr};
  LocalAetherIdentity::ptr identity_;
  ChatConnectionUiState connection_ui_{};
  std::optional<std::chrono::steady_clock::time_point> connect_t0_;
  std::string last_connect_uid_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_WIN_CONNECTION_BAR_PRESENTER_H_
