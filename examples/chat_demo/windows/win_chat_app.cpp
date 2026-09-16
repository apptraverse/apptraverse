#include "win_chat_app.h"

#include <commctrl.h>
#include <richedit.h>
#include <shlobj.h>
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "apptraverse/object_serialization.h"
#include "chat_commands.h"
#include "win32_fatal.h"

#pragma comment(lib, "comctl32.lib")

namespace apptraverse::example::chat_demo {
namespace {

wchar_t const kMainChatWindowClass[] = L"AppTraverseWinChatMainWindow";
wchar_t const kNotifyChatWindowClass[] = L"AppTraverseWinChatNotifyWindow";

inline constexpr UINT WM_CHAT_PUBLISHED = WM_APP + 10;
inline constexpr UINT WM_CHAT_STATUS_NOTIFY = WM_APP + 11;

std::wstring Utf8ToUtf16(std::string const& utf8) {
  if (utf8.empty()) {
    return L"";
  }
  int const len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (len <= 0) {
    return L"";
  }
  std::wstring utf16(static_cast<std::size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), utf16.data(), len);
  return utf16;
}

std::string Utf16ToUtf8(std::wstring const& utf16) {
  if (utf16.empty()) {
    return "";
  }
  int const len = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
  if (len <= 0) {
    return "";
  }
  std::string utf8(static_cast<std::size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), utf8.data(), len, nullptr, nullptr);
  return utf8;
}

std::wstring GetWindowTextString(HWND hwnd) {
  int const len = GetWindowTextLengthW(hwnd);
  if (len <= 0) {
    return L"";
  }
  std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
  GetWindowTextW(hwnd, text.data(), len + 1);
  text.resize(static_cast<std::size_t>(len));
  return text;
}

std::filesystem::path GetDefaultStateDirectory() {
  PWSTR path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
    std::filesystem::path p{path};
    CoTaskMemFree(path);
    return p / "AppTraverseChat";
  }
  return std::filesystem::current_path() / "AppTraverseChat";
}

}  // namespace

WinChatApp::WinChatApp() = default;

WinChatApp::~WinChatApp() {
  if (profile_lock_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(profile_lock_handle_);
    profile_lock_handle_ = INVALID_HANDLE_VALUE;
  }
  if (richedit_module_ != nullptr) {
    FreeLibrary(richedit_module_);
    richedit_module_ = nullptr;
  }
}

LRESULT CALLBACK WinChatApp::DraftEditSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass_id, DWORD_PTR ref_data) {
  auto* app = reinterpret_cast<WinChatApp*>(ref_data);
  if (msg == WM_KEYDOWN) {
    bool const ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (wparam == VK_RETURN && ctrl_down) {
      if (app != nullptr) {
        app->OnSendDraftClicked();
      }
      return 0;
    }
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK WinChatApp::MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* app = reinterpret_cast<WinChatApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app != nullptr) {
    return app->HandleMain(hwnd, msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WinChatApp::CreateControls(HWND hwnd) {
  HINSTANCE const hinst = GetModuleHandleW(nullptr);

  // Left chat list
  chat_list_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(101), hinst, nullptr);

  // Top connection controls
  admin_id_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(102), hinst, nullptr);

  aether_uid_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(103), hinst, nullptr);

  open_btn_hwnd_ = CreateWindowExW(
      0, L"BUTTON", L"Open",
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(104), hinst, nullptr);

  // Status and presence labels
  status_label_hwnd_ = CreateWindowExW(
      0, L"STATIC", L"Starting...",
      WS_CHILD | WS_VISIBLE | SS_LEFT,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(105), hinst, nullptr);

  presence_label_hwnd_ = CreateWindowExW(
      0, L"STATIC", L"",
      WS_CHILD | WS_VISIBLE | SS_RIGHT,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(106), hinst, nullptr);

  // RichEdit transcript
  transcript_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(107), hinst, nullptr);

  SendMessageW(transcript_hwnd_, EM_SETEVENTMASK, 0,
               SendMessageW(transcript_hwnd_, EM_GETEVENTMASK, 0, 0) | ENM_SCROLL);

  // Multiline draft edit
  draft_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(108), hinst, nullptr);

  SetWindowSubclass(draft_hwnd_, &WinChatApp::DraftEditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

  // Send button
  send_btn_hwnd_ = CreateWindowExW(
      0, L"BUTTON", L"Send",
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(109), hinst, nullptr);
}

void WinChatApp::LayoutControls(int width, int height) {
  int const margin = 12;
  int const gap = 12;
  int const left_width = 240;
  int const top_bar_height = 28;
  int const status_bar_height = 20;
  int const draft_height = 90;
  int const send_btn_width = 80;

  int const right_x = margin + left_width + gap;
  int const right_width = width - right_x - margin;

  // Chat list on the left
  MoveWindow(chat_list_hwnd_, margin, margin + top_bar_height + gap,
             left_width, height - 2 * margin - top_bar_height - gap - status_bar_height, TRUE);

  // Top bar: [Admin ID] [UID optional] [Open]
  int const admin_w = 120;
  int const open_w = 60;
  int const uid_w = (right_width > (admin_w + open_w + 2 * gap)) ? (right_width - admin_w - open_w - 2 * gap) : 100;

  MoveWindow(admin_id_hwnd_, right_x, margin, admin_w, top_bar_height, TRUE);
  MoveWindow(aether_uid_hwnd_, right_x + admin_w + gap, margin, uid_w, top_bar_height, TRUE);
  MoveWindow(open_btn_hwnd_, right_x + admin_w + gap + uid_w + gap, margin, open_w, top_bar_height, TRUE);

  // Transcript
  int const transcript_y = margin + top_bar_height + gap;
  int const transcript_h = height - transcript_y - draft_height - status_bar_height - 2 * gap - margin;
  MoveWindow(transcript_hwnd_, right_x, transcript_y, right_width, transcript_h, TRUE);

  // Draft and Send button
  int const draft_y = transcript_y + transcript_h + gap;
  int const draft_w = right_width - send_btn_width - gap;
  MoveWindow(draft_hwnd_, right_x, draft_y, draft_w, draft_height, TRUE);
  MoveWindow(send_btn_hwnd_, right_x + draft_w + gap, draft_y, send_btn_width, draft_height, TRUE);

  // Status bar and presence label at the bottom
  int const status_y = height - margin - status_bar_height;
  MoveWindow(status_label_hwnd_, margin, status_y, width / 2 - margin, status_bar_height, TRUE);
  MoveWindow(presence_label_hwnd_, width / 2, status_y, width / 2 - margin, status_bar_height, TRUE);
}

void WinChatApp::OnDraftChanged() {
  if (applying_view_) {
    return;
  }
  std::wstring const wtext = GetWindowTextString(draft_hwnd_);
  std::string const text = Utf16ToUtf8(wtext);
  active_chat_local_draft_ = text;
  ++local_edit_revision_;
  if (active_entry_id_.is_valid()) {
    session_.EditDraft(active_entry_id_, text, local_edit_revision_);
  }
}

void WinChatApp::OnSendDraftClicked() {
  std::wstring const wtext = GetWindowTextString(draft_hwnd_);
  std::string const current_text = Utf16ToUtf8(wtext);
  if (current_text.empty() || !active_entry_id_.is_valid()) {
    return;
  }
  ++local_edit_revision_;
  session_.SendDraft(active_entry_id_, current_text, local_edit_revision_);
}

void WinChatApp::OnOpenPeerClicked() {
  std::wstring const wadmin = GetWindowTextString(admin_id_hwnd_);
  std::wstring const wuid = GetWindowTextString(aether_uid_hwnd_);

  std::string const admin_id = Utf16ToUtf8(wadmin);
  std::string const peer_uid = Utf16ToUtf8(wuid);

  if (admin_id.empty()) {
    return;
  }

  OpenPeerRequest req{
      .peer_admin_id = admin_id,
      .peer_aether_uid = peer_uid.empty() ? std::nullopt : std::optional<std::string>(peer_uid),
  };
  session_.OpenPeer(std::move(req));
}

void WinChatApp::OnChatSelectionChanged() {
  int const sel = static_cast<int>(SendMessageW(chat_list_hwnd_, LB_GETCURSEL, 0, 0));
  if (sel == LB_ERR || !ui_workspace_.is_valid()) {
    return;
  }
  if (sel >= 0 && static_cast<std::size_t>(sel) < ui_workspace_->chats.size()) {
    auto entry = ui_workspace_->chats[static_cast<std::size_t>(sel)];
    if (entry.is_valid() && entry.id() != active_entry_id_) {
      SaveCurrentDraftAndScroll();
      active_entry_id_ = entry.id();
      session_.SelectChat(active_entry_id_);
      RestoreDraftAndScroll();
    }
  }
}

void WinChatApp::SaveCurrentDraftAndScroll() {
  if (!active_entry_id_.is_valid() || !ui_workspace_.is_valid()) {
    return;
  }
  // Draft already pushed via EditDraft
  ScrollAnchor anchor;
  CaptureScrollAnchor(anchor);
  session_.SaveScroll(active_entry_id_, anchor);
}

void WinChatApp::CaptureScrollAnchor(ScrollAnchor& anchor) {
  anchor.follow_tail = true;
  POINT pt{5, 5};
  LONG const char_idx = static_cast<LONG>(SendMessageW(transcript_hwnd_, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&pt)));
  if (char_idx < 0 || message_char_positions_.empty()) {
    return;
  }

  // Find containing message
  for (std::size_t i = 0; i < message_char_positions_.size(); ++i) {
    if (char_idx >= message_char_positions_[i].second) {
      if (i + 1 == message_char_positions_.size() || char_idx < message_char_positions_[i + 1].second) {
        anchor.first_visible_message = message_char_positions_[i].first;
        anchor.follow_tail = (i + 1 == message_char_positions_.size());
        anchor.offset_from_message_top = 0.0;
        break;
      }
    }
  }
}

void WinChatApp::RestoreScrollAnchor(ScrollAnchor const& anchor) {
  if (anchor.follow_tail) {
    SendMessageW(transcript_hwnd_, WM_VSCROLL, SB_BOTTOM, 0);
    return;
  }
  for (auto const& [msg_id, pos] : message_char_positions_) {
    if (msg_id == anchor.first_visible_message) {
      SendMessageW(transcript_hwnd_, EM_SETSEL, pos, pos);
      SendMessageW(transcript_hwnd_, EM_SCROLLCARET, 0, 0);
      return;
    }
  }
}

void WinChatApp::RestoreWindowGeometry() {
  if (geometry_restored_ || !ui_workspace_.is_valid() || main_hwnd_ == nullptr) {
    return;
  }
  geometry_restored_ = true;
  auto const& bounds = ui_workspace_->desktop_bounds;
  if (!bounds.valid || bounds.width <= 0 || bounds.height <= 0) {
    return;
  }

  RECT normal_rc{bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height};

  // Check if coordinates belong to an existing monitor
  HMONITOR hmon = MonitorFromRect(&normal_rc, MONITOR_DEFAULTTONULL);
  if (hmon == nullptr) {
    // Off-screen: clamp onto nearest available monitor
    hmon = MonitorFromRect(&normal_rc, MONITOR_DEFAULTTONEAREST);
    if (hmon != nullptr) {
      MONITORINFO mi{};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfoW(hmon, &mi) != 0) {
        int const w = std::min(bounds.width, static_cast<int>(mi.rcWork.right - mi.rcWork.left));
        int const h = std::min(bounds.height, static_cast<int>(mi.rcWork.bottom - mi.rcWork.top));
        int x = bounds.x;
        int y = bounds.y;
        if (x + w > mi.rcWork.right) {
          x = mi.rcWork.right - w;
        }
        if (x < mi.rcWork.left) {
          x = mi.rcWork.left;
        }
        if (y + h > mi.rcWork.bottom) {
          y = mi.rcWork.bottom - h;
        }
        if (y < mi.rcWork.top) {
          y = mi.rcWork.top;
        }
        normal_rc = RECT{x, y, x + w, y + h};
      }
    }
  }

  WINDOWPLACEMENT wp{};
  wp.length = sizeof(wp);
  wp.rcNormalPosition = normal_rc;
  wp.showCmd = bounds.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
  SetWindowPlacement(main_hwnd_, &wp);
}

void WinChatApp::RestoreDraftAndScroll() {
  if (!ui_workspace_.is_valid() || !active_entry_id_.is_valid()) {
    return;
  }
  ChatEntry::ptr current_entry;
  for (auto const& e : ui_workspace_->chats) {
    if (e.is_valid() && e.id() == active_entry_id_) {
      current_entry = e;
      break;
    }
  }
  if (!current_entry.is_valid()) {
    return;
  }

  applying_view_ = true;
  active_chat_local_draft_ = current_entry->draft;
  SetWindowTextW(draft_hwnd_, Utf8ToUtf16(current_entry->draft).c_str());
  UpdateTranscript();
  RestoreScrollAnchor(current_entry->scroll);
  applying_view_ = false;
}

void WinChatApp::UpdateTranscript() {
  message_char_positions_.clear();
  if (!ui_workspace_.is_valid() || !active_entry_id_.is_valid()) {
    SetWindowTextW(transcript_hwnd_, L"");
    return;
  }

  ChatEntry::ptr current_entry;
  for (auto const& e : ui_workspace_->chats) {
    if (e.is_valid() && e.id() == active_entry_id_) {
      current_entry = e;
      break;
    }
  }

  if (!current_entry.is_valid() || !current_entry->room.is_valid()) {
    SetWindowTextW(transcript_hwnd_, L"");
    return;
  }

  auto const& messages = current_entry->room->messages;
  std::wstring transcript_text;
  for (auto const& msg : messages) {
    LONG const pos = static_cast<LONG>(transcript_text.size());
    message_char_positions_.emplace_back(msg.id, pos);

    std::wstring author;
    if (!ui_workspace_->local_endpoint_uid.empty() &&
        msg.id.origin_uid == ui_workspace_->local_endpoint_uid) {
      author = L"You";
    } else {
      author = Utf8ToUtf16(current_entry->display_name.empty() ? current_entry->peer_admin_id : current_entry->display_name);
    }

    transcript_text += L"[" + author + L"]: " + Utf8ToUtf16(msg.text) + L"\r\n";
  }

  SetWindowTextW(transcript_hwnd_, transcript_text.c_str());
}

void WinChatApp::UpdateStatusLine() {
  auto const status = session_.GetRuntimeStatus();
  std::wstring status_text;
  if (!status.error_text.empty()) {
    status_text = L"Error: " + Utf8ToUtf16(status.error_text);
  } else if (!status.local_endpoint_uid.empty()) {
    status_text = L"UID: " + Utf8ToUtf16(status.local_endpoint_uid);
  } else {
    status_text = L"Starting...";
  }
  SetWindowTextW(status_label_hwnd_, status_text.c_str());

  // Update presence label for active chat
  std::wstring presence_text;
  if (ui_workspace_.is_valid() && active_entry_id_.is_valid()) {
    for (auto const& e : ui_workspace_->chats) {
      if (e.is_valid() && e.id() == active_entry_id_) {
        if (e->peer_link.is_valid()) {
          auto it = status.remote_presence.find(e->peer_link->EndpointUid());
          if (it != status.remote_presence.end()) {
            switch (it->second) {
              case PeerPresence::kOnline:
                presence_text = L"Online";
                break;
              case PeerPresence::kConnecting:
                presence_text = L"Connecting...";
                break;
              case PeerPresence::kOffline:
                presence_text = L"Offline";
                break;
              default:
                presence_text = L"Unknown";
                break;
            }
          }
        }
        break;
      }
    }
  }
  SetWindowTextW(presence_label_hwnd_, presence_text.c_str());

  // Update Send button enabled status
  bool send_enabled = false;
  if (!status.local_endpoint_uid.empty() && ui_workspace_.is_valid() && active_entry_id_.is_valid()) {
    for (auto const& e : ui_workspace_->chats) {
      if (e.is_valid() && e.id() == active_entry_id_) {
        send_enabled = e->room.is_valid();
        break;
      }
    }
  }
  EnableWindow(send_btn_hwnd_, send_enabled ? TRUE : FALSE);
}

void WinChatApp::UpdateUiFromWorkspace() {
  if (!ui_workspace_.is_valid()) {
    return;
  }

  applying_view_ = true;

  // 1. Populate chat list
  SendMessageW(chat_list_hwnd_, LB_RESETCONTENT, 0, 0);
  int sel_index = -1;
  for (std::size_t i = 0; i < ui_workspace_->chats.size(); ++i) {
    auto const& entry = ui_workspace_->chats[i];
    if (!entry.is_valid()) {
      continue;
    }
    std::wstring display = Utf8ToUtf16(entry->display_name.empty() ? entry->peer_admin_id : entry->display_name);
    SendMessageW(chat_list_hwnd_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
    if (ui_workspace_->selected_chat_id == entry.id() ||
        (!active_entry_id_.is_valid() && i == 0)) {
      sel_index = static_cast<int>(i);
      active_entry_id_ = entry.id();
    }
  }
  if (sel_index >= 0) {
    SendMessageW(chat_list_hwnd_, LB_SETCURSEL, sel_index, 0);
  }

  // 2. Draft update: only if local revision caught up or chat changed
  ChatEntry::ptr current_entry;
  for (auto const& e : ui_workspace_->chats) {
    if (e.is_valid() && e.id() == active_entry_id_) {
      current_entry = e;
      break;
    }
  }

  if (current_entry.is_valid()) {
    if (local_edit_revision_ <= last_published_edit_revision_) {
      if (active_chat_local_draft_ != current_entry->draft) {
        active_chat_local_draft_ = current_entry->draft;
        SetWindowTextW(draft_hwnd_, Utf8ToUtf16(current_entry->draft).c_str());
      }
    }
  }

  // 3. Transcript and scroll
  ScrollAnchor current_anchor;
  CaptureScrollAnchor(current_anchor);
  UpdateTranscript();
  if (current_entry.is_valid()) {
    if (current_anchor.follow_tail) {
      RestoreScrollAnchor(ScrollAnchor{.follow_tail = true});
    } else {
      RestoreScrollAnchor(current_anchor);
    }
  }

  UpdateStatusLine();
  applying_view_ = false;
}

void WinChatApp::TryFinishClosing() {
  if (!closing_) {
    return;
  }
  auto const status = session_.GetRuntimeStatus();
  if (status.lifecycle_state == SessionLifecycleState::kStopped ||
      status.lifecycle_state == SessionLifecycleState::kFailed) {
    session_.Join();
    if (main_hwnd_ != nullptr) {
      DestroyWindow(main_hwnd_);
    }
  }
}

void WinChatApp::ApplyPublicationFromSession() {
  while (auto update = session_.TryTakeUiUpdate()) {
    if (update->publication_bytes.has_value() &&
        !update->publication_bytes->empty()) {
      auto const& bytes = *update->publication_bytes;
      ByteSource in{bytes.data(), bytes.size()};
      if (!ui_workspace_.is_valid()) {
        ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
        auto loaded = LoadInitialPublication(in, *ui_domain_, ui_storage_);
        ui_workspace_ = ChatWorkspace::ptr::MakeFromThis(
            static_cast<ChatWorkspace*>(&*loaded));
        RestoreWindowGeometry();
      } else {
        ApplyStructuralPublication(in, *ui_domain_, ui_storage_);
      }
    }
    if (active_entry_id_.is_valid()) {
      auto it =
          update->processed_edit_revisions_by_entry.find(active_entry_id_);
      if (it != update->processed_edit_revisions_by_entry.end()) {
        last_published_edit_revision_ = it->second;
      }
    }
  }
  UpdateUiFromWorkspace();
  TryFinishClosing();
}

LRESULT WinChatApp::HandleMain(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_CREATE) {
    CreateControls(hwnd);
    return 0;
  }
  if (msg == WM_SIZE) {
    LayoutControls(LOWORD(lparam), HIWORD(lparam));
    return 0;
  }
  if (msg == WM_GETMINMAXINFO) {
    auto* mmi = reinterpret_cast<MINMAXINFO*>(lparam);
    mmi->ptMinTrackSize.x = 720;
    mmi->ptMinTrackSize.y = 480;
    return 0;
  }
  if (msg == WM_COMMAND) {
    WORD const id = LOWORD(wparam);
    WORD const code = HIWORD(wparam);
    if (id == 104 && code == BN_CLICKED) {
      OnOpenPeerClicked();
      return 0;
    }
    if (id == 109 && code == BN_CLICKED) {
      OnSendDraftClicked();
      return 0;
    }
    if (id == 101 && code == LBN_SELCHANGE) {
      OnChatSelectionChanged();
      return 0;
    }
    if (id == 108 && code == EN_CHANGE) {
      OnDraftChanged();
      return 0;
    }
    if (id == 107 && code == EN_VSCROLL) {
      if (!applying_view_ && active_entry_id_.is_valid()) {
        auto const now = std::chrono::steady_clock::now();
        if (now - last_scroll_save_time_ >= std::chrono::milliseconds(200)) {
          last_scroll_save_time_ = now;
          ScrollAnchor anchor;
          CaptureScrollAnchor(anchor);
          session_.SaveScroll(active_entry_id_, anchor);
        }
      }
      return 0;
    }
  }
  if (msg == WM_CHAT_PUBLISHED || msg == WM_CHAT_STATUS_NOTIFY) {
    ApplyPublicationFromSession();
    return 0;
  }
  if (msg == WM_CLOSE) {
    if (closing_) {
      return 0;
    }
    SaveCurrentDraftAndScroll();
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp) != 0) {
      bool const maximized = (wp.showCmd == SW_SHOWMAXIMIZED) ||
                             ((wp.flags & WPF_RESTORETOMAXIMIZED) != 0);
      DesktopBounds bounds{
          .valid = true,
          .x = wp.rcNormalPosition.left,
          .y = wp.rcNormalPosition.top,
          .width = wp.rcNormalPosition.right - wp.rcNormalPosition.left,
          .height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top,
          .maximized = maximized,
      };
      if (bounds.width > 0 && bounds.height > 0) {
        session_.SaveBounds(bounds);
      }
    }
    closing_ = true;
    EnableWindow(hwnd, FALSE);
    session_.RequestStop();
    TryFinishClosing();
    return 0;
  }
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WinChatApp::Run(ChatLaunchOptions options) {
  richedit_module_ = LoadLibraryW(L"Msftedit.dll");
  if (richedit_module_ == nullptr) {
    FatalWin32("LoadLibraryW Msftedit.dll", GetLastError());
  }

  std::filesystem::path state_dir;
  if (options.state_dir.has_value()) {
    state_dir = *options.state_dir;
  } else {
    state_dir = GetDefaultStateDirectory();
  }
  std::filesystem::create_directories(state_dir);

  // Profile locking
  std::filesystem::path lock_file = state_dir / "profile.lock";
  profile_lock_handle_ = CreateFileW(
      lock_file.c_str(), GENERIC_READ | GENERIC_WRITE,
      0, // Exclusive access, no sharing
      nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (profile_lock_handle_ == INVALID_HANDLE_VALUE) {
    MessageBoxW(nullptr, L"Error: Profile already open in another process.",
                L"AppTraverse Chat", MB_ICONERROR | MB_OK);
    return 1;
  }

  // Register main window class
  HINSTANCE const hinst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &WinChatApp::MainWndProc;
  wc.hInstance = hinst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kMainChatWindowClass;
  if (RegisterClassW(&wc) == 0) {
    FatalWin32("RegisterClassW MainChatWindow", GetLastError());
  }

  main_hwnd_ = CreateWindowExW(
      0, kMainChatWindowClass, L"AppTraverse Chat",
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
      nullptr, nullptr, hinst, this);

  if (main_hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW MainChatWindow", GetLastError());
  }

  ShowWindow(main_hwnd_, SW_SHOWNORMAL);
  UpdateWindow(main_hwnd_);

  // Start ChatSession
  ChatSessionConfig cfg{
      .state_dir = state_dir,
      .initial_open_peer = options.open_peer,
  };

  session_.Start(std::move(cfg), [this]() {
    if (main_hwnd_ != nullptr) {
      PostMessageW(main_hwnd_, WM_CHAT_PUBLISHED, 0, 0);
      PostMessageW(main_hwnd_, WM_CHAT_STATUS_NOTIFY, 0, 0);
    }
  });

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  return static_cast<int>(msg.wParam);
}

}  // namespace apptraverse::example::chat_demo
