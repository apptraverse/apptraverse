#ifndef APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstring>
#include <functional>
#include <string>

#include "aether-miscpp/format/format.h"

#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_trace.h"
#include "../common/chat_peer_schedule.h"
#include "../common/room_membership_controller.h"
#include "win_participant_list_presenter.h"

namespace apptraverse {

// Version 1: room Host/Client controls (runtime-only HWNDs).
class WinChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(WinChatPresenter, ChatPresenter, 1)

 protected:
  WinChatPresenter() = default;

 public:
  explicit WinChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()

  enum class RoomUiMode { kHost, kClient };

  // Runtime-only UI wiring. Not reflected / not serialized.
  void ConfigureRoom(RoomUiMode mode, std::string local_uid_display) {
    room_mode_ = mode;
    local_uid_display_ = std::move(local_uid_display);
    ApplyRoomUiState();
  }

  void SetJoinHandler(std::function<void(std::string host_uid)> handler) {
    join_handler_ = std::move(handler);
  }

  void SetCopyRoomIdHandler(std::function<void()> handler) {
    copy_handler_ = std::move(handler);
  }

  void SetSubmitTextHandler(std::function<bool(std::string)> handler) {
    submit_text_ = std::move(handler);
  }

  void SetSendEnabled(bool enabled) {
    send_enabled_ = enabled;
    if (send_ != nullptr) {
      EnableWindow(send_, enabled ? TRUE : FALSE);
    }
    if (edit_ != nullptr) {
      EnableWindow(edit_, enabled ? TRUE : FALSE);
    }
  }

  void SetJoinEnabled(bool enabled) {
    join_enabled_ = enabled;
    if (host_uid_edit_ != nullptr) {
      EnableWindow(host_uid_edit_, enabled ? TRUE : FALSE);
    }
    if (join_ != nullptr) {
      EnableWindow(join_, enabled ? TRUE : FALSE);
    }
  }

  void SetStatusText(std::string text) {
    status_text_ = std::move(text);
    if (status_ != nullptr) {
      SetWindowTextW(status_, Utf8ToWide(status_text_).c_str());
    }
  }

  void ApplyRoomUiState(chat::RoomUiStatus status = chat::RoomUiStatus::kDisconnected,
                        std::string const& error = {}) {
    bool const is_host = room_mode_ == RoomUiMode::kHost;
    bool const active = status == chat::RoomUiStatus::kActive;
    bool const connecting =
        status == chat::RoomUiStatus::kConnecting ||
        status == chat::RoomUiStatus::kSyncing ||
        status == chat::RoomUiStatus::kWaitingForOwnJoin;

    if (room_id_ != nullptr) {
      ShowWindow(room_id_, is_host ? SW_SHOW : SW_HIDE);
    }
    if (copy_ != nullptr) {
      ShowWindow(copy_, is_host ? SW_SHOW : SW_HIDE);
    }

    bool const show_join = !is_host && !active;
    if (host_uid_edit_ != nullptr) {
      ShowWindow(host_uid_edit_, show_join ? SW_SHOW : SW_HIDE);
    }
    if (join_ != nullptr) {
      ShowWindow(join_, show_join ? SW_SHOW : SW_HIDE);
    }
    if (status_ != nullptr) {
      ShowWindow(status_, (!is_host && (show_join || !error.empty())) ? SW_SHOW
                                                                     : SW_HIDE);
    }

    if (is_host) {
      SetSendEnabled(true);
      if (room_id_ != nullptr && !local_uid_display_.empty()) {
        SetWindowTextW(room_id_,
                       Utf8ToWide("Room ID: " + local_uid_display_).c_str());
      }
    } else if (status == chat::RoomUiStatus::kError) {
      SetSendEnabled(false);
      SetJoinEnabled(true);
      SetStatusText(error.empty() ? std::string{"Error"} : error);
    } else if (connecting) {
      SetSendEnabled(false);
      SetJoinEnabled(false);
      SetStatusText("Connecting…");
    } else if (active) {
      SetSendEnabled(true);
      SetJoinEnabled(false);
      SetStatusText({});
    } else {
      SetSendEnabled(false);
      SetJoinEnabled(true);
      SetStatusText({});
    }
  }

  void CreateControls(HWND parent) {
    parent_ = parent;

    room_id_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_READONLY | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(10)),
        GetModuleHandleW(nullptr), nullptr);

    copy_ = CreateWindowExW(
        0, L"BUTTON", L"Copy", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(6)),
        GetModuleHandleW(nullptr), nullptr);

    host_uid_edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(11)),
        GetModuleHandleW(nullptr), nullptr);

    join_ = CreateWindowExW(
        0, L"BUTTON", L"Join", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(5)),
        GetModuleHandleW(nullptr), nullptr);

    status_ = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(12)),
        GetModuleHandleW(nullptr), nullptr);

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

    send_ = CreateWindowExW(
        0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0,
        0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(3)),
        GetModuleHandleW(nullptr), nullptr);

    participants_.CreateControls(parent);
    ApplyRoomUiState();
  }

  void Layout(int width, int height) {
    int const margin = 8;
    int const edit_h = 28;
    int const send_w = 80;
    int const copy_w = 64;
    int const join_w = 72;
    int y = margin;
    bool const is_host = room_mode_ == RoomUiMode::kHost;

    if (is_host) {
      int const room_w = width - 3 * margin - copy_w;
      if (room_id_ != nullptr) {
        MoveWindow(room_id_, margin, y, room_w > 0 ? room_w : 0, edit_h, TRUE);
      }
      if (copy_ != nullptr) {
        MoveWindow(copy_, width - margin - copy_w, y, copy_w, edit_h, TRUE);
      }
      y += edit_h + margin;
    } else if (host_uid_edit_ != nullptr &&
               IsWindowVisible(host_uid_edit_)) {
      int const field_w = width - 3 * margin - join_w;
      MoveWindow(host_uid_edit_, margin, y, field_w > 0 ? field_w : 0, edit_h,
                 TRUE);
      if (join_ != nullptr) {
        MoveWindow(join_, width - margin - join_w, y, join_w, edit_h, TRUE);
      }
      y += edit_h + margin;
      if (status_ != nullptr && IsWindowVisible(status_)) {
        MoveWindow(status_, margin, y, width - 2 * margin, edit_h, TRUE);
        y += edit_h + margin;
      }
    }

    int const bottom = height - margin - edit_h;
    int const bottom_reserve = margin + edit_h;
    int const content_w =
        participants_.Layout(width, height, margin, y, bottom_reserve);
    int const transcript_h = bottom - y;
    if (transcript_ != nullptr) {
      MoveWindow(transcript_, margin, y, content_w,
                 transcript_h > 0 ? transcript_h : 0, TRUE);
    }
    int const edit_w = content_w - 2 * margin - send_w;
    if (edit_ != nullptr) {
      MoveWindow(edit_, margin, bottom, edit_w > 0 ? edit_w : 0, edit_h, TRUE);
    }
    if (send_ != nullptr) {
      MoveWindow(send_, margin + (edit_w > 0 ? edit_w : 0) + margin, bottom,
                 send_w, edit_h, TRUE);
    }
  }

  void OnSendClicked() {
    Trace("MESSAGE_UI_SEND_CLICK");
    if (!send_enabled_ || edit_ == nullptr || !submit_text_) {
      return;
    }
    int const len = GetWindowTextLengthW(edit_);
    std::wstring wide;
    wide.resize(static_cast<std::size_t>(len) + 1);
    int const copied = GetWindowTextW(edit_, &wide[0], len + 1);
    wide.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
    while (!wide.empty() &&
           (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' ')) {
      wide.pop_back();
    }
    if (wide.empty()) {
      return;
    }

    if (submit_text_(WideToUtf8(wide))) {
      SetWindowTextW(edit_, L"");
    }
  }

  void OnJoinClicked() {
    Trace("UI_JOIN_CLICK");
    if (!join_enabled_ || host_uid_edit_ == nullptr || !join_handler_) {
      return;
    }
    int const len = GetWindowTextLengthW(host_uid_edit_);
    std::wstring wide;
    wide.resize(static_cast<std::size_t>(len) + 1);
    int const copied = GetWindowTextW(host_uid_edit_, &wide[0], len + 1);
    wide.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
    auto text = WideToUtf8(wide);
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t')) {
      text.erase(text.begin());
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
            text.back() == '\n')) {
      text.pop_back();
    }
    if (text.empty()) {
      return;
    }
    join_handler_(std::move(text));
  }

  void OnCopyClicked() {
    if (copy_handler_) {
      copy_handler_();
      return;
    }
    if (local_uid_display_.empty()) {
      return;
    }
    CopyUtf8ToClipboard(local_uid_display_);
  }

  // UI-thread only. Caller holds model shared_lock for the duration — Win32
  // updates happen while the lock is held. Does not call Load/Save/Commit;
  // business must EnsurePresentable() first under exclusive lock.
  void PresentLive(
      Chat::ptr const& chat, Client::ptr const& local_client,
      chat::RoomMembershipController const& room,
      ae::Uid const& host_uid,
      WinParticipantListPresenter::GetPeerPresenceFn const& get_peer_presence,
      chat::LocalPresenceStatus local_presence,
      std::uint32_t dirty_bits) {
    AssertUiThread("WinChatPresenter::PresentLive");
    constexpr std::uint32_t kDirtyChat = 1u << 1;
    constexpr std::uint32_t kDirtyParticipants = 1u << 2;
    constexpr std::uint32_t kDirtyRoomControls = 1u << 3;

    if ((dirty_bits & kDirtyChat) != 0 && transcript_ != nullptr &&
        chat.is_valid() && chat.is_loaded()) {
      Trace("UI_CHAT_PRESENT_BEGIN",
            ae::Format("dirty={}", dirty_bits));
      Trace("MESSAGE_UI_PRESENT_BEGIN",
            ae::Format("dirty={}", dirty_bits));
      std::string utf8;
      for (auto const& record : chat->journal) {
        if (!record.event.is_valid() || !record.event.is_loaded()) {
          continue;
        }
        auto const class_id = record.event->GetClassId();
        if (class_id == JoinClientEvent::kClassId) {
          auto join = JoinClientEvent::ptr{record.event};
          if (!join.is_loaded() || !join->client.is_valid() ||
              !join->client.is_loaded() || join->client->name.empty()) {
            continue;
          }
          utf8 += "* ";
          utf8 += join->client->name;
          utf8 += " joined\r\n";
        } else if (class_id == AddMessageEvent::kClassId) {
          auto msg = AddMessageEvent::ptr{record.event};
          if (!msg.is_loaded() || !msg->author.is_valid()) {
            continue;
          }
          std::string author =
              msg->author.is_loaded() ? msg->author->name : std::string{};
          utf8 += author;
          utf8 += ": ";
          utf8 += msg->text;
          utf8 += "\r\n";
        }
      }
      std::wstring text = Utf8ToWide(utf8);
      SetWindowTextW(transcript_, text.c_str());
      SendMessageW(transcript_, EM_SETSEL, static_cast<WPARAM>(text.size()),
                   static_cast<LPARAM>(text.size()));
      SendMessageW(transcript_, EM_SCROLLCARET, 0, 0);
      Trace("UI_CHAT_PRESENT_END", ae::Format("dirty={}", dirty_bits));
      Trace("MESSAGE_UI_PRESENT_END", ae::Format("dirty={}", dirty_bits));
    }

    if ((dirty_bits & kDirtyParticipants) != 0) {
      std::uint32_t local_id = 0;
      if (local_client.is_valid()) {
        local_id = local_client.id().id();
      }
      participants_.PresentLive(room.ActiveParticipants(), host_uid, local_id,
                                get_peer_presence, local_presence);
    }

    if ((dirty_bits & kDirtyRoomControls) != 0) {
      ApplyRoomUiState(room.ui_status(), room.error());
    }
  }

  void RefreshTranscript() {}

  HWND transcript() const { return transcript_; }
  HWND edit() const { return edit_; }
  HWND send() const { return send_; }

 private:
  static void CopyUtf8ToClipboard(std::string const& utf8) {
    auto wide = Utf8ToWide(utf8);
    if (!OpenClipboard(nullptr)) {
      return;
    }
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE,
                              (wide.size() + 1) * sizeof(wchar_t));
    if (mem == nullptr) {
      CloseClipboard();
      return;
    }
    auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
    if (dst != nullptr) {
      memcpy(dst, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(mem);
      SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
  }

  static std::string WideToUtf8(std::wstring const& wide) {
    if (wide.empty()) {
      return {};
    }
    int const size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                         static_cast<int>(wide.size()), nullptr,
                                         0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
  }

  static std::wstring Utf8ToWide(std::string const& utf8) {
    if (utf8.empty()) {
      return {};
    }
    int const size =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                            static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        out.data(), size);
    return out;
  }

  HWND parent_{nullptr};
  HWND room_id_{nullptr};
  HWND copy_{nullptr};
  HWND host_uid_edit_{nullptr};
  HWND join_{nullptr};
  HWND status_{nullptr};
  HWND transcript_{nullptr};
  HWND edit_{nullptr};
  HWND send_{nullptr};
  WinParticipantListPresenter participants_;
  RoomUiMode room_mode_{RoomUiMode::kHost};
  std::string local_uid_display_;
  std::string status_text_;
  bool send_enabled_{false};
  bool join_enabled_{true};
  std::function<void(std::string)> join_handler_;
  std::function<void()> copy_handler_;
  std::function<bool(std::string)> submit_text_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_
