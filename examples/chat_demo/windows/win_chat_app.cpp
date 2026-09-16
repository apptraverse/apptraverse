#include "win_chat_app.h"

#include <commctrl.h>
#include <imm.h>
#include <richedit.h>
#include <shlobj.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "apptraverse/object_serialization.h"
#include "chat_commands.h"
#include "chat_launch_ipc.h"
#include "profile_lock.h"
#include "win32_fatal.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "imm32.lib")

namespace apptraverse::example::chat_demo {
namespace {

wchar_t const kMainChatWindowClass[] = L"AppTraverseWinChatMainWindow";

inline constexpr UINT WM_CHAT_PUBLISHED = WM_APP + 10;
inline constexpr UINT WM_CHAT_STATUS_NOTIFY = WM_APP + 11;
inline constexpr UINT WM_CHAT_TEST_SNAPSHOT = WM_APP + 20;
inline constexpr UINT WM_CHAT_TEST_APPLY = WM_APP + 21;
inline constexpr UINT WM_CHAT_TEST_SELECT = WM_APP + 22;

inline constexpr int kLogicalDpi = 96;
inline constexpr int kBottomProximityLogicalPx = 8;
inline constexpr int kScrollCoalesceMs = 200;
inline constexpr int kGeometrySettleTimerId = 1;
inline constexpr int kGeometrySettleMs = 400;

#ifndef EM_GETSCROLLPOS
#  define EM_GETSCROLLPOS (WM_USER + 221)
#endif
#ifndef EM_SETSCROLLPOS
#  define EM_SETSCROLLPOS (WM_USER + 222)
#endif

std::wstring Utf8ToUtf16(std::string const& utf8) {
  if (utf8.empty()) {
    return L"";
  }
  int const len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                      nullptr, 0);
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
  int const len = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()),
                                      nullptr, 0, nullptr, nullptr);
  if (len <= 0) {
    return "";
  }
  std::string utf8(static_cast<std::size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), utf8.data(), len,
                      nullptr, nullptr);
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
  MessageBoxW(nullptr,
              L"Failed to resolve LocalAppData folder; using current directory.",
              L"AppTraverse Chat", MB_ICONWARNING | MB_OK);
  return std::filesystem::current_path() / "AppTraverseChat";
}

bool RegisterMainWindowClassOnce(HINSTANCE hinst) {
  WNDCLASSW existing{};
  if (GetClassInfoW(hinst, kMainChatWindowClass, &existing) != 0) {
    return true;
  }
  WNDCLASSW wc{};
  wc.lpfnWndProc = &WinChatApp::MainWndProc;
  wc.hInstance = hinst;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kMainChatWindowClass;
  if (RegisterClassW(&wc) == 0) {
    FatalWin32("RegisterClassW MainChatWindow", GetLastError());
  }
  return true;
}

void RequireControl(HWND hwnd, char const* label) {
  if (hwnd == nullptr) {
    FatalWin32(label, GetLastError());
  }
}

LONG GetRichEditScrollPosY(HWND hwnd) {
  POINT pt{};
  SendMessageW(hwnd, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&pt));
  return pt.y;
}

void SetRichEditScrollPosY(HWND hwnd, LONG y) {
  POINT pt{};
  SendMessageW(hwnd, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&pt));
  pt.y = y;
  SendMessageW(hwnd, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&pt));
}

void SaveRichEditSelection(HWND hwnd, CHARRANGE& range) {
  SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
}

void RestoreRichEditSelection(HWND hwnd, CHARRANGE const& range) {
  SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
}

}  // namespace

WinChatApp::WinChatApp() = default;

WinChatApp::~WinChatApp() {
  ShutdownSessionAndResources();
}

void WinChatApp::ShutdownSessionAndResources() {
  DestroyIpcNotifyWindow();
  profile_lock_ = ProfileLock{};
  if (richedit_module_ != nullptr) {
    FreeLibrary(richedit_module_);
    richedit_module_ = nullptr;
  }
}

int WinChatApp::WindowLogicalDpi(HWND hwnd) const {
  if (hwnd == nullptr) {
    return kLogicalDpi;
  }
  UINT dpi = GetDpiForWindow(hwnd);
  if (dpi == 0) {
    return kLogicalDpi;
  }
  return static_cast<int>(dpi);
}

LRESULT CALLBACK WinChatApp::DraftEditSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR subclass_id,
    DWORD_PTR ref_data) {
  auto* app = reinterpret_cast<WinChatApp*>(ref_data);
  if (app != nullptr && msg == WM_CHAR && wparam == L'\r') {
    bool const ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl_down) {
      app->OnSendDraftClicked();
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

  chat_list_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS, 0, 0, 0, 0, hwnd,
      reinterpret_cast<HMENU>(101), hinst, nullptr);
  RequireControl(chat_list_hwnd_, "CreateWindowExW chat list");

  admin_id_hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                                   reinterpret_cast<HMENU>(102), hinst, nullptr);
  RequireControl(admin_id_hwnd_, "CreateWindowExW admin id");

  aether_uid_hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                                     reinterpret_cast<HMENU>(103), hinst, nullptr);
  RequireControl(aether_uid_hwnd_, "CreateWindowExW aether uid");

  open_btn_hwnd_ = CreateWindowExW(0, L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(104), hinst,
                                   nullptr);
  RequireControl(open_btn_hwnd_, "CreateWindowExW open button");

  status_label_hwnd_ = CreateWindowExW(0, L"STATIC", L"Starting...", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(105), hinst,
                                       nullptr);
  RequireControl(status_label_hwnd_, "CreateWindowExW status label");

  presence_label_hwnd_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0,
                                         0, 0, hwnd, reinterpret_cast<HMENU>(106), hinst, nullptr);
  RequireControl(presence_label_hwnd_, "CreateWindowExW presence label");

  transcript_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 0, 0,
      hwnd, reinterpret_cast<HMENU>(107), hinst, nullptr);
  RequireControl(transcript_hwnd_, "CreateWindowExW transcript");

  SendMessageW(transcript_hwnd_, EM_SETEVENTMASK, 0,
               SendMessageW(transcript_hwnd_, EM_GETEVENTMASK, 0, 0) | ENM_SCROLL);

  draft_hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 0,
      0, hwnd, reinterpret_cast<HMENU>(108), hinst, nullptr);
  RequireControl(draft_hwnd_, "CreateWindowExW draft");

  SetWindowSubclass(draft_hwnd_, &WinChatApp::DraftEditSubclassProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));

  send_btn_hwnd_ = CreateWindowExW(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,
                                   0, 0, 0, hwnd, reinterpret_cast<HMENU>(109), hinst, nullptr);
  RequireControl(send_btn_hwnd_, "CreateWindowExW send button");
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

  MoveWindow(chat_list_hwnd_, margin, margin + top_bar_height + gap, left_width,
             height - 2 * margin - top_bar_height - gap - status_bar_height, TRUE);

  int const admin_w = 120;
  int const open_w = 60;
  int const uid_w =
      (right_width > (admin_w + open_w + 2 * gap)) ? (right_width - admin_w - open_w - 2 * gap)
                                                   : 100;

  MoveWindow(admin_id_hwnd_, right_x, margin, admin_w, top_bar_height, TRUE);
  MoveWindow(aether_uid_hwnd_, right_x + admin_w + gap, margin, uid_w, top_bar_height, TRUE);
  MoveWindow(open_btn_hwnd_, right_x + admin_w + gap + uid_w + gap, margin, open_w, top_bar_height,
             TRUE);

  int const transcript_y = margin + top_bar_height + gap;
  int const transcript_h =
      height - transcript_y - draft_height - status_bar_height - 2 * gap - margin;
  MoveWindow(transcript_hwnd_, right_x, transcript_y, right_width, transcript_h, TRUE);

  int const draft_y = transcript_y + transcript_h + gap;
  int const draft_w = right_width - send_btn_width - gap;
  MoveWindow(draft_hwnd_, right_x, draft_y, draft_w, draft_height, TRUE);
  MoveWindow(send_btn_hwnd_, right_x + draft_w + gap, draft_y, send_btn_width, draft_height, TRUE);

  int const status_y = height - margin - status_bar_height;
  MoveWindow(status_label_hwnd_, margin, status_y, width / 2 - margin, status_bar_height, TRUE);
  MoveWindow(presence_label_hwnd_, width / 2, status_y, width / 2 - margin, status_bar_height,
             TRUE);
}

WinChatApp::EntryViewState& WinChatApp::ViewStateFor(ae::ObjId entry_id) {
  return entry_views_[entry_id];
}

ChatEntry::ptr WinChatApp::FindUiEntry(ae::ObjId entry_id) const {
  if (!ui_workspace_.is_valid() || !entry_id.is_valid()) {
    return {};
  }
  for (auto const& entry : ui_workspace_->chats) {
    if (entry.is_valid() && entry.id() == entry_id) {
      return entry;
    }
  }
  return {};
}

bool WinChatApp::IsImeComposing(HWND hwnd) const {
  HIMC const imc = ImmGetContext(hwnd);
  if (imc == nullptr) {
    return false;
  }
  LONG const comp_len = ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0);
  ImmReleaseContext(hwnd, imc);
  return comp_len > 0;
}

bool WinChatApp::IsNearBottom(HWND hwnd) const {
  int const dpi = WindowLogicalDpi(hwnd);
  int const tolerance = MulDiv(kBottomProximityLogicalPx, dpi, kLogicalDpi);

  SCROLLINFO si{};
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  if (GetScrollInfo(hwnd, SB_VERT, &si) != 0 && si.nMax > 0) {
    int const remaining =
        static_cast<int>(si.nMax) - static_cast<int>(si.nPos) - static_cast<int>(si.nPage);
    return remaining <= tolerance;
  }

  RECT client{};
  GetClientRect(hwnd, &client);
  POINT bottom_left{0, client.bottom - 1};
  LONG const bottom_char =
      static_cast<LONG>(SendMessageW(hwnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&bottom_left)));
  LONG const text_len = static_cast<LONG>(GetWindowTextLengthW(hwnd));
  return bottom_char >= text_len - 1;
}

LONG WinChatApp::MessageCharPosition(SharedEventId const& message_id) const {
  for (auto const& [id, pos] : message_char_positions_) {
    if (id == message_id) {
      return pos;
    }
  }
  return -1;
}

void WinChatApp::RebuildMessageCharPositions(ChatEntry::ptr const& entry) {
  message_char_positions_.clear();
  if (!entry.is_valid() || !entry->room.is_valid()) {
    return;
  }
  LONG pos = 0;
  for (auto const& msg : entry->room->messages) {
    message_char_positions_.emplace_back(msg.id, pos);
    pos += static_cast<LONG>(FormatTranscriptLine(entry, msg).size());
  }
}

std::wstring WinChatApp::FormatTranscriptLine(ChatEntry::ptr const& entry,
                                               MessageValue const& msg) const {
  std::wstring author;
  if (ui_workspace_.is_valid() && !ui_workspace_->local_endpoint_uid.empty() &&
      msg.id.origin_uid == ui_workspace_->local_endpoint_uid) {
    author = L"You";
  } else {
    author = Utf8ToUtf16(entry->display_name.empty() ? entry->peer_admin_id : entry->display_name);
  }
  return L"[" + author + L"]: " + Utf8ToUtf16(msg.text) + L"\r\n";
}

bool WinChatApp::CanAppendTranscript(ChatEntry::ptr const& entry) const {
  if (!entry.is_valid() || !entry->room.is_valid()) {
    return false;
  }
  if (transcript_cache_.entry_id != entry.id()) {
    return false;
  }
  if (transcript_cache_.message_count == 0 ||
      transcript_cache_.message_count >= entry->room->messages.size()) {
    return false;
  }
  auto const& prior = entry->room->messages[transcript_cache_.message_count - 1];
  return prior.id == transcript_cache_.last_message_id;
}

void WinChatApp::AppendTranscriptLines(ChatEntry::ptr const& entry, std::size_t from_index) {
  CHARRANGE saved_sel{};
  SaveRichEditSelection(transcript_hwnd_, saved_sel);

  SendMessageW(transcript_hwnd_, EM_SETSEL, static_cast<WPARAM>(-1),
               static_cast<LPARAM>(-1));

  for (std::size_t i = from_index; i < entry->room->messages.size(); ++i) {
    auto const& msg = entry->room->messages[i];
    std::wstring const line = FormatTranscriptLine(entry, msg);
    LONG const pos = static_cast<LONG>(SendMessageW(transcript_hwnd_, WM_GETTEXTLENGTH, 0, 0));
    message_char_positions_.emplace_back(msg.id, pos);
    SendMessageW(transcript_hwnd_, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(line.c_str()));
    transcript_cache_.message_count = i + 1;
    transcript_cache_.last_message_id = msg.id;
  }

  RestoreRichEditSelection(transcript_hwnd_, saved_sel);
}

void WinChatApp::OnDraftChanged() {
  if (applying_view_ || !active_entry_id_.is_valid()) {
    return;
  }
  std::wstring const wtext = GetWindowTextString(draft_hwnd_);
  std::string const text = Utf16ToUtf8(wtext);

  EntryViewState& view = ViewStateFor(active_entry_id_);
  view.draft.local_draft = text;
  ++view.draft.local_edit_revision;
  SendMessageW(draft_hwnd_, EM_GETSEL, reinterpret_cast<WPARAM>(&view.draft.sel_start),
                reinterpret_cast<LPARAM>(&view.draft.sel_end));

  session_.EditDraft(active_entry_id_, text, view.draft.local_edit_revision);
}

void WinChatApp::OnSendDraftClicked() {
  if (!active_entry_id_.is_valid()) {
    return;
  }
  std::wstring const wtext = GetWindowTextString(draft_hwnd_);
  std::string const current_text = Utf16ToUtf8(wtext);
  if (current_text.empty()) {
    return;
  }

  EntryViewState& view = ViewStateFor(active_entry_id_);
  ++view.draft.local_edit_revision;
  pending_send_revision_ = view.draft.local_edit_revision;
  local_send_error_.clear();
  session_.SendDraft(active_entry_id_, current_text, view.draft.local_edit_revision);
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
  ChatEntry::ptr selected_entry;
  int list_index = 0;
  for (auto const& entry : ui_workspace_->chats) {
    if (!entry.is_valid()) {
      continue;
    }
    if (list_index == sel) {
      selected_entry = entry;
      break;
    }
    ++list_index;
  }
  if (!selected_entry.is_valid() || selected_entry.id() == active_entry_id_) {
    return;
  }

  SaveCurrentDraftAndScroll(/*flush_scroll=*/true);

  active_entry_id_ = selected_entry.id();
  pending_user_selection_ = selected_entry.id();
  session_.SelectChat(active_entry_id_);

  RestoreDraftAndScroll(selected_entry.id());
}

void WinChatApp::FlushPendingScrollSave() {
  if (!pending_scroll_save_.has_value() || !active_entry_id_.is_valid()) {
    return;
  }
  session_.SaveScroll(active_entry_id_, *pending_scroll_save_);
  EntryViewState& view = ViewStateFor(active_entry_id_);
  view.live_scroll = *pending_scroll_save_;
  view.live_scroll_valid = true;
  pending_scroll_save_.reset();
}

void WinChatApp::SaveCurrentDraftAndScroll(bool flush_scroll) {
  if (!active_entry_id_.is_valid()) {
    return;
  }
  std::wstring const wdraft = GetWindowTextString(draft_hwnd_);
  EntryViewState& view = ViewStateFor(active_entry_id_);
  view.draft.local_draft = Utf16ToUtf8(wdraft);
  SendMessageW(draft_hwnd_, EM_GETSEL, reinterpret_cast<WPARAM>(&view.draft.sel_start),
                reinterpret_cast<LPARAM>(&view.draft.sel_end));
  if (flush_scroll) {
    FlushPendingScrollSave();
  }
  ScrollAnchor anchor{};
  CaptureScrollAnchor(anchor);
  session_.SaveScroll(active_entry_id_, anchor);
  view.live_scroll = anchor;
  view.live_scroll_valid = true;
}

void WinChatApp::CaptureScrollAnchor(ScrollAnchor& anchor) {
  anchor = ScrollAnchor{};
  if (transcript_hwnd_ == nullptr) {
    anchor.follow_tail = true;
    return;
  }

  if (IsNearBottom(transcript_hwnd_)) {
    anchor.follow_tail = true;
    return;
  }

  int const dpi = WindowLogicalDpi(transcript_hwnd_);
  LONG const scroll_y = GetRichEditScrollPosY(transcript_hwnd_);

  RECT format_rect{};
  SendMessageW(transcript_hwnd_, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format_rect));

  POINT viewport_origin{0, format_rect.top};
  LONG const first_char = static_cast<LONG>(SendMessageW(
      transcript_hwnd_, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&viewport_origin)));
  if (first_char < 0 || message_char_positions_.empty()) {
    anchor.follow_tail = true;
    return;
  }

  SharedEventId visible_message{};
  LONG visible_char = message_char_positions_.front().second;
  for (std::size_t i = 0; i < message_char_positions_.size(); ++i) {
    LONG const start = message_char_positions_[i].second;
    LONG const end = (i + 1 < message_char_positions_.size())
                         ? message_char_positions_[i + 1].second
                         : static_cast<LONG>(GetWindowTextLengthW(transcript_hwnd_));
    if (first_char >= start && first_char < end) {
      visible_message = message_char_positions_[i].first;
      visible_char = start;
      break;
    }
    if (first_char >= start) {
      visible_message = message_char_positions_[i].first;
      visible_char = start;
    }
  }

  if (visible_message.origin_sequence == 0 && visible_message.origin_uid.empty()) {
    anchor.follow_tail = true;
    return;
  }

  POINT msg_pt{};
  SendMessageW(transcript_hwnd_, EM_POSFROMCHAR, 0,
               static_cast<LPARAM>(visible_char));
  double const viewport_top_logical =
      static_cast<double>(scroll_y) * kLogicalDpi / static_cast<double>(dpi);
  double const message_top_logical =
      static_cast<double>(msg_pt.y - format_rect.top + scroll_y) * kLogicalDpi /
      static_cast<double>(dpi);

  anchor.follow_tail = false;
  anchor.first_visible_message = visible_message;
  anchor.offset_from_message_top = message_top_logical - viewport_top_logical;
}

void WinChatApp::RestoreScrollAnchor(ScrollAnchor const& anchor) {
  if (transcript_hwnd_ == nullptr) {
    return;
  }

  CHARRANGE saved_sel{};
  SaveRichEditSelection(transcript_hwnd_, saved_sel);

  if (anchor.follow_tail) {
    SendMessageW(transcript_hwnd_, WM_VSCROLL, SB_BOTTOM, 0);
    RestoreRichEditSelection(transcript_hwnd_, saved_sel);
    return;
  }

  LONG const msg_char = MessageCharPosition(anchor.first_visible_message);
  if (msg_char < 0) {
    SendMessageW(transcript_hwnd_, WM_VSCROLL, SB_TOP, 0);
    RestoreRichEditSelection(transcript_hwnd_, saved_sel);
    return;
  }

  SendMessageW(transcript_hwnd_, EM_SETSEL, static_cast<WPARAM>(msg_char),
               static_cast<LPARAM>(msg_char));
  SendMessageW(transcript_hwnd_, EM_SCROLLCARET, 0, 0);

  int const dpi = WindowLogicalDpi(transcript_hwnd_);
  int const target_offset_phys =
      MulDiv(static_cast<int>(std::lround(anchor.offset_from_message_top)), dpi, kLogicalDpi);

  RECT format_rect{};
  SendMessageW(transcript_hwnd_, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format_rect));
  POINT msg_pt{};
  SendMessageW(transcript_hwnd_, EM_POSFROMCHAR, 0, static_cast<LPARAM>(msg_char));
  LONG const scroll_y = GetRichEditScrollPosY(transcript_hwnd_);
  int const current_offset = msg_pt.y - format_rect.top;
  int const delta = target_offset_phys - current_offset;
  if (delta != 0) {
    SetRichEditScrollPosY(transcript_hwnd_, scroll_y + delta);
  }

  RestoreRichEditSelection(transcript_hwnd_, saved_sel);
}

DesktopBounds WinChatApp::CaptureBoundsLogical(HWND hwnd) const {
  WINDOWPLACEMENT wp{};
  wp.length = sizeof(wp);
  GetWindowPlacement(hwnd, &wp);
  int const dpi = WindowLogicalDpi(hwnd);
  RECT const& rc = wp.rcNormalPosition;
  bool const maximized =
      wp.showCmd == SW_SHOWMAXIMIZED || ((wp.flags & WPF_RESTORETOMAXIMIZED) != 0);
  return DesktopBounds{
      .valid = true,
      .x = MulDiv(rc.left, kLogicalDpi, dpi),
      .y = MulDiv(rc.top, kLogicalDpi, dpi),
      .width = MulDiv(rc.right - rc.left, kLogicalDpi, dpi),
      .height = MulDiv(rc.bottom - rc.top, kLogicalDpi, dpi),
      .maximized = maximized,
  };
}

void WinChatApp::ApplyBoundsLogical(DesktopBounds const& bounds) {
  if (!bounds.valid || bounds.width <= 0 || bounds.height <= 0 || main_hwnd_ == nullptr) {
    return;
  }

  int const dpi = WindowLogicalDpi(main_hwnd_);
  RECT normal_rc{
      MulDiv(bounds.x, dpi, kLogicalDpi),
      MulDiv(bounds.y, dpi, kLogicalDpi),
      MulDiv(bounds.x + bounds.width, dpi, kLogicalDpi),
      MulDiv(bounds.y + bounds.height, dpi, kLogicalDpi),
  };

  HMONITOR hmon = MonitorFromRect(&normal_rc, MONITOR_DEFAULTTONULL);
  if (hmon == nullptr) {
    hmon = MonitorFromRect(&normal_rc, MONITOR_DEFAULTTONEAREST);
    if (hmon != nullptr) {
      MONITORINFO mi{};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfoW(hmon, &mi) != 0) {
        int const w =
            std::min(normal_rc.right - normal_rc.left, mi.rcWork.right - mi.rcWork.left);
        int const h =
            std::min(normal_rc.bottom - normal_rc.top, mi.rcWork.bottom - mi.rcWork.top);
        int x = normal_rc.left;
        int y = normal_rc.top;
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

void WinChatApp::RestoreWindowGeometry() {
  if (geometry_restored_ || !ui_workspace_.is_valid()) {
    return;
  }
  geometry_restored_ = true;
  ApplyBoundsLogical(ui_workspace_->desktop_bounds);
}

void WinChatApp::PersistWindowGeometry() {
  if (main_hwnd_ == nullptr || closing_) {
    return;
  }
  DesktopBounds const bounds = CaptureBoundsLogical(main_hwnd_);
  if (bounds.width > 0 && bounds.height > 0) {
    session_.SaveBounds(bounds);
  }
}

void WinChatApp::RestoreDraftAndScroll(ae::ObjId entry_id) {
  if (!entry_id.is_valid()) {
    return;
  }
  ChatEntry::ptr entry = FindUiEntry(entry_id);
  if (!entry.is_valid()) {
    return;
  }

  applying_view_ = true;
  displayed_entry_id_ = entry_id;
  UpdateDraftFromModel(entry, /*chat_switched=*/true);

  UpdateTranscript(entry, /*chat_switched=*/true, &entry->scroll);
  applying_view_ = false;
}

void WinChatApp::UpdateDraftFromModel(ChatEntry::ptr const& entry, bool chat_switched) {
  if (!entry.is_valid()) {
    return;
  }
  EntryViewState& view = ViewStateFor(entry.id());
  if (IsImeComposing(draft_hwnd_)) {
    return;
  }

  bool const may_replace =
      chat_switched || view.draft.local_edit_revision <= view.draft.last_published_edit_revision;
  if (!may_replace) {
    return;
  }

  std::string display_draft = entry->draft;
  if (chat_switched && !view.draft.local_draft.empty()) {
    display_draft = view.draft.local_draft;
  } else if (view.draft.local_draft != entry->draft) {
    display_draft = entry->draft;
  } else {
    display_draft = view.draft.local_draft;
  }

  if (view.draft.local_draft != display_draft) {
    view.draft.local_draft = display_draft;
  }
  std::wstring const wdraft = Utf8ToUtf16(display_draft);
  if (GetWindowTextString(draft_hwnd_) != wdraft) {
    SendMessageW(draft_hwnd_, EM_SETSEL, 0, 0);
    SetWindowTextW(draft_hwnd_, wdraft.c_str());
  }

  if (chat_switched) {
    SendMessageW(draft_hwnd_, EM_SETSEL, view.draft.sel_start, view.draft.sel_end);
  }
}

void WinChatApp::UpdateTranscript(ChatEntry::ptr const& entry, bool chat_switched,
                                   ScrollAnchor const* restore_anchor) {
  ScrollAnchor captured{};
  bool const preserve_live = !chat_switched && restore_anchor == nullptr;
  if (preserve_live) {
    CaptureScrollAnchor(captured);
  }

  if (!entry.is_valid() || !entry->room.is_valid()) {
    message_char_positions_.clear();
    transcript_cache_ = {};
    SetWindowTextW(transcript_hwnd_, L"");
    return;
  }

  bool const did_append = !chat_switched && CanAppendTranscript(entry);
  if (chat_switched || !did_append) {
    RebuildMessageCharPositions(entry);
    std::wstring transcript_text;
    transcript_text.reserve(entry->room->messages.size() * 64);
    for (auto const& msg : entry->room->messages) {
      transcript_text += FormatTranscriptLine(entry, msg);
    }
    if (GetWindowTextString(transcript_hwnd_) != transcript_text) {
      SetWindowTextW(transcript_hwnd_, transcript_text.c_str());
    }
    transcript_cache_.entry_id = entry.id();
    transcript_cache_.message_count = entry->room->messages.size();
    if (!entry->room->messages.empty()) {
      transcript_cache_.last_message_id = entry->room->messages.back().id;
    }
  } else {
    AppendTranscriptLines(entry, transcript_cache_.message_count);
  }

  displayed_entry_id_ = entry.id();

  if (restore_anchor != nullptr) {
    RestoreScrollAnchor(*restore_anchor);
  } else if (preserve_live) {
    if (captured.follow_tail) {
      RestoreScrollAnchor(ScrollAnchor{.follow_tail = true});
    } else {
      RestoreScrollAnchor(captured);
    }
  }
}

void WinChatApp::UpdateChatListSelection() {
  if (!ui_workspace_.is_valid()) {
    return;
  }

  ae::ObjId target = active_entry_id_;
  if (pending_user_selection_.has_value()) {
    target = *pending_user_selection_;
  } else if (ui_workspace_->selected_chat_id.is_valid()) {
    target = ui_workspace_->selected_chat_id;
  }

  int sel_index = -1;
  int list_index = 0;
  SendMessageW(chat_list_hwnd_, LB_RESETCONTENT, 0, 0);
  for (auto const& entry : ui_workspace_->chats) {
    if (!entry.is_valid()) {
      continue;
    }
    std::wstring const display =
        Utf8ToUtf16(entry->display_name.empty() ? entry->peer_admin_id : entry->display_name);
    SendMessageW(chat_list_hwnd_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
    if (entry.id() == target) {
      sel_index = list_index;
    }
    if (!active_entry_id_.is_valid() && list_index == 0) {
      sel_index = 0;
      active_entry_id_ = entry.id();
    }
    ++list_index;
  }
  if (sel_index >= 0) {
    SendMessageW(chat_list_hwnd_, LB_SETCURSEL, sel_index, 0);
  }
}

void WinChatApp::UpdateStatusLine() {
  auto const status = session_.GetRuntimeStatus();
  std::wstring status_text;
  if (!local_send_error_.empty()) {
    status_text = L"Error: " + Utf8ToUtf16(local_send_error_);
  } else if (!status.error_text.empty()) {
    status_text = L"Error: " + Utf8ToUtf16(status.error_text);
  } else if (status.lifecycle_state == SessionLifecycleState::kFailed) {
    status_text = L"Connection failed";
  } else if (status.local_connectivity == LocalConnectivityState::kUnknown) {
    status_text = L"Starting...";
  } else if (status.local_connectivity == LocalConnectivityState::kOffline) {
    status_text = L"Local Aether offline";
  } else if (!status.local_endpoint_uid.empty()) {
    status_text = L"UID: " + Utf8ToUtf16(status.local_endpoint_uid);
  } else {
    status_text = L"Starting...";
  }
  SetWindowTextW(status_label_hwnd_, status_text.c_str());

  std::wstring presence_text;
  auto const entry = FindUiEntry(active_entry_id_);
  if (entry.is_valid() && entry->peer_link.is_valid()) {
    std::string const& peer_uid = entry->peer_link->EndpointUid();
    auto const boot = status.room_bootstrap_by_peer_uid.find(peer_uid);
    if (boot != status.room_bootstrap_by_peer_uid.end() &&
        boot->second != RoomBootstrapState::kComplete) {
      presence_text = boot->second == RoomBootstrapState::kPending ? L"Syncing..."
                                                                   : L"Waiting room";
    } else {
      auto it = status.remote_presence.find(peer_uid);
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
    if (!entry->room.is_valid() || entry->room->messages.empty()) {
      // keep presence only
    } else {
      SharedEventId const last_id = entry->room->messages.back().id;
      auto const delivery = status.delivery_by_event_id.find(last_id);
      if (delivery != status.delivery_by_event_id.end()) {
        switch (delivery->second) {
          case MessageDeliveryState::kDelivered:
            presence_text += L" | Delivered";
            break;
          case MessageDeliveryState::kSending:
            presence_text += L" | Sending";
            break;
          case MessageDeliveryState::kQueued:
            presence_text += L" | Queued";
            break;
          default:
            break;
        }
      }
    }
  }
  SetWindowTextW(presence_label_hwnd_, presence_text.c_str());

  bool send_enabled = false;
  if (!status.local_endpoint_uid.empty() && entry.is_valid() && entry->room.is_valid()) {
    send_enabled = true;
  }
  EnableWindow(send_btn_hwnd_, send_enabled ? TRUE : FALSE);
}

void WinChatApp::UpdateUiFromWorkspace(bool chat_switched) {
  if (!ui_workspace_.is_valid()) {
    return;
  }

  applying_view_ = true;
  UpdateChatListSelection();

  bool const selection_changed =
      chat_switched || (displayed_entry_id_.is_valid() && displayed_entry_id_ != active_entry_id_);
  if (selection_changed) {
    displayed_entry_id_ = active_entry_id_;
  }

  auto const entry = FindUiEntry(active_entry_id_);
  if (entry.is_valid()) {
    UpdateDraftFromModel(entry, selection_changed);
    ScrollAnchor const* restore = nullptr;
    if (selection_changed) {
      EntryViewState const& view = ViewStateFor(entry.id());
      restore = view.live_scroll_valid ? &view.live_scroll : &entry->scroll;
    }
    UpdateTranscript(entry, selection_changed, restore);
  } else {
    UpdateTranscript({}, selection_changed, nullptr);
  }

  UpdateStatusLine();
  applying_view_ = false;
}

void WinChatApp::ConsumeUiUpdates() {
  bool chat_switched = false;
  bool had_publication = false;

  while (auto update = session_.TryTakeUiUpdate()) {
    if (update->publication_bytes.has_value() && !update->publication_bytes->empty()) {
      had_publication = true;
      auto const& bytes = *update->publication_bytes;
      ByteSource in{bytes.data(), bytes.size()};
      if (!ui_workspace_.is_valid()) {
        ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
        auto loaded = LoadInitialPublication(in, *ui_domain_, ui_storage_);
        ui_workspace_ = ChatWorkspace::ptr::MakeFromThis(static_cast<ChatWorkspace*>(&*loaded));
        RestoreWindowGeometry();
        if (!active_entry_id_.is_valid() && ui_workspace_->selected_chat_id.is_valid()) {
          active_entry_id_ = ui_workspace_->selected_chat_id;
        }
      } else {
        ae::ObjId const prior_selected = active_entry_id_;
        ApplyStructuralPublication(in, *ui_domain_, ui_storage_);
        if (pending_user_selection_.has_value()) {
          // Keep user selection; do not bounce to stale selected_chat_id.
        } else if (ui_workspace_->selected_chat_id.is_valid() &&
                   ui_workspace_->selected_chat_id != prior_selected) {
          chat_switched = true;
          active_entry_id_ = ui_workspace_->selected_chat_id;
        }
      }
    }

    if (update->selected_chat_ack.has_value()) {
      if (pending_user_selection_ == *update->selected_chat_ack) {
        pending_user_selection_.reset();
      }
    }

    for (auto const& [entry_id, rev] : update->processed_edit_revisions_by_entry) {
      EntryViewState& view = ViewStateFor(entry_id);
      view.draft.last_published_edit_revision = rev;
      if (entry_id == active_entry_id_ && pending_send_revision_ == rev) {
        pending_send_revision_ = 0;
        local_send_error_.clear();
      }
    }
  }

  if (pending_send_revision_ != 0) {
    local_send_error_ = "Message not sent";
  }

  if (had_publication || ui_workspace_.is_valid()) {
    UpdateUiFromWorkspace(chat_switched);
  } else {
    UpdateStatusLine();
  }
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
      main_hwnd_ = nullptr;
    }
  }
}

void WinChatApp::ApplyPublicationFromSession() {
  ConsumeUiUpdates();
  if (session_.GetRuntimeStatus().lifecycle_state == SessionLifecycleState::kReady) {
    ApplyPendingIpcOpenPeer();
  }
  TryFinishClosing();
}

bool WinChatApp::TryQueryGuiSnapshot(WinChatGuiSnapshot& out) {
  if (main_hwnd_ == nullptr) {
    return false;
  }
  DWORD window_tid = 0;
  GetWindowThreadProcessId(main_hwnd_, &window_tid);
  if (GetCurrentThreadId() != window_tid) {
    SendMessageW(main_hwnd_, WM_CHAT_TEST_SNAPSHOT, 0, reinterpret_cast<LPARAM>(&out));
    return true;
  }
  out = BuildGuiSnapshot();
  return true;
}

WinChatGuiSnapshot WinChatApp::BuildGuiSnapshot() {
  ApplyPublicationFromSession();
  WinChatGuiSnapshot snap{};
  snap.workspace_ready = ui_workspace_.is_valid();
  snap.active_entry_id = active_entry_id_;
  if (ui_workspace_.is_valid()) {
    snap.chat_count = static_cast<int>(ui_workspace_->chats.size());
    snap.bounds = ui_workspace_->desktop_bounds;
    auto const entry = FindUiEntry(active_entry_id_);
    if (entry.is_valid()) {
      snap.model_scroll = entry->scroll;
    }
  }
  snap.list_selection = static_cast<int>(SendMessageW(chat_list_hwnd_, LB_GETCURSEL, 0, 0));
  snap.draft = GetWindowTextString(draft_hwnd_);
  SendMessageW(draft_hwnd_, EM_GETSEL, reinterpret_cast<WPARAM>(&snap.draft_sel_start),
                reinterpret_cast<LPARAM>(&snap.draft_sel_end));

  WINDOWPLACEMENT wp{};
  wp.length = sizeof(wp);
  if (GetWindowPlacement(main_hwnd_, &wp) != 0) {
    snap.maximized =
        wp.showCmd == SW_SHOWMAXIMIZED || ((wp.flags & WPF_RESTORETOMAXIMIZED) != 0);
    snap.bounds = CaptureBoundsLogical(main_hwnd_);
  }

  CaptureScrollAnchor(snap.measured_scroll);
  snap.status_text = GetWindowTextString(status_label_hwnd_);
  return snap;
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
  if (msg == WM_DPICHANGED) {
    auto* const suggested = reinterpret_cast<RECT*>(lparam);
    SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
  }
  if (msg == WM_EXITSIZEMOVE) {
    KillTimer(hwnd, kGeometrySettleTimerId);
    SetTimer(hwnd, kGeometrySettleTimerId, kGeometrySettleMs, nullptr);
    return 0;
  }
  if (msg == WM_TIMER && wparam == kGeometrySettleTimerId) {
    KillTimer(hwnd, kGeometrySettleTimerId);
    PersistWindowGeometry();
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
        ScrollAnchor anchor{};
        CaptureScrollAnchor(anchor);
        pending_scroll_save_ = anchor;
        EntryViewState& view = ViewStateFor(active_entry_id_);
        view.live_scroll = anchor;
        view.live_scroll_valid = true;
        auto const now = std::chrono::steady_clock::now();
        if (now - last_scroll_save_time_ >= std::chrono::milliseconds(kScrollCoalesceMs)) {
          last_scroll_save_time_ = now;
          FlushPendingScrollSave();
        }
      }
      return 0;
    }
  }
  if (msg == WM_CHAT_PUBLISHED || msg == WM_CHAT_STATUS_NOTIFY) {
    ApplyPublicationFromSession();
    return 0;
  }
  if (msg == WM_CHAT_TEST_SNAPSHOT) {
    auto* out = reinterpret_cast<WinChatGuiSnapshot*>(lparam);
    *out = BuildGuiSnapshot();
    return 1;
  }
  if (msg == WM_CHAT_TEST_APPLY) {
    ApplyPublicationFromSession();
    return 1;
  }
  if (msg == WM_CHAT_TEST_SELECT) {
    SendMessageW(chat_list_hwnd_, LB_SETCURSEL, wparam, 0);
    OnChatSelectionChanged();
    return 0;
  }
  if (msg == WM_CLOSE) {
    if (closing_) {
      return 0;
    }
    SaveCurrentDraftAndScroll(/*flush_scroll=*/true);
    PersistWindowGeometry();
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

LRESULT CALLBACK WinChatApp::IpcNotifyWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                               LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  auto* app = reinterpret_cast<WinChatApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (app != nullptr && msg == WM_COPYDATA) {
    auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lparam);
    return static_cast<LRESULT>(app->HandleLaunchIpcCopyData(cds));
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WinChatApp::CreateIpcNotifyWindow(HINSTANCE hinst) {
  std::wstring const ipc_class = ProfileRoutingWindowClass(profile_key_);
  WNDCLASSW wc{};
  if (GetClassInfoW(hinst, ipc_class.c_str(), &wc) == 0) {
    wc.lpfnWndProc = &WinChatApp::IpcNotifyWndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = ipc_class.c_str();
    RegisterClassW(&wc);
  }
  std::wstring const title = ProfileRoutingWindowTitle(profile_key_);
  ipc_notify_hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, ipc_class.c_str(), title.c_str(), WS_POPUP,
                                     0, 0, 0, 0, nullptr, nullptr, hinst, this);
  if (ipc_notify_hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW IpcNotify", GetLastError());
  }
  ShowWindow(ipc_notify_hwnd_, SW_HIDE);
}

void WinChatApp::DestroyIpcNotifyWindow() {
  if (ipc_notify_hwnd_ != nullptr) {
    DestroyWindow(ipc_notify_hwnd_);
    ipc_notify_hwnd_ = nullptr;
  }
}

LaunchIpcReply WinChatApp::HandleLaunchIpcCopyData(COPYDATASTRUCT* cds) {
  if (cds == nullptr || cds->dwData != kLaunchIpcCopyDataMagic) {
    return LaunchIpcReply::kInvalidPayload;
  }
  if (cds->cbData > kLaunchIpcMaxPayloadBytes || cds->lpData == nullptr) {
    return LaunchIpcReply::kOversized;
  }
  std::vector<std::uint8_t> bytes(cds->cbData);
  std::memcpy(bytes.data(), cds->lpData, cds->cbData);

  ChatLaunchIpcPayload payload;
  if (!DecodeLaunchIpcPayload(bytes, payload)) {
    return LaunchIpcReply::kInvalidPayload;
  }
  LaunchIpcReply const valid = ValidateLaunchIpcPayload(payload, profile_key_);
  if (valid != LaunchIpcReply::kAccepted) {
    return valid;
  }

  if (closing_) {
    return LaunchIpcReply::kRejected;
  }

  if (session_.GetRuntimeStatus().lifecycle_state == SessionLifecycleState::kReady) {
    session_.OpenPeer(payload.open_peer);
  } else {
    pending_ipc_open_peer_ = std::move(payload.open_peer);
  }

  if (main_hwnd_ != nullptr) {
    AllowSetForegroundWindow(GetCurrentProcessId());
    ShowWindow(main_hwnd_, SW_RESTORE);
    SetForegroundWindow(main_hwnd_);
  }
  return LaunchIpcReply::kAccepted;
}

void WinChatApp::ApplyPendingIpcOpenPeer() {
  if (pending_ipc_open_peer_.has_value()) {
    session_.OpenPeer(*pending_ipc_open_peer_);
    pending_ipc_open_peer_.reset();
  }
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
  profile_key_ = NormalizeProfileKey(state_dir);

  ProfileLock candidate;
  ProfileLock::AcquireResult const lock_result =
      ProfileLock::TryAcquire(state_dir, candidate);
  if (lock_result == ProfileLock::AcquireResult::kBusy) {
    if (options.open_peer.has_value()) {
      ForwardLaunchResult const forwarded =
          TryForwardLaunchToPrimary(profile_key_, *options.open_peer);
      if (forwarded == ForwardLaunchResult::kAccepted) {
        return 0;
      }
      if (forwarded == ForwardLaunchResult::kTimeout) {
        MessageBoxW(nullptr,
                    L"Error: Existing chat window is not responding.",
                    L"AppTraverse Chat", MB_ICONERROR | MB_OK);
        return 1;
      }
      if (forwarded == ForwardLaunchResult::kRejected) {
        MessageBoxW(nullptr, L"Error: Launch request rejected by active profile.",
                    L"AppTraverse Chat", MB_ICONERROR | MB_OK);
        return 1;
      }
    }
    MessageBoxW(nullptr, L"Error: Profile already open in another process.", L"AppTraverse Chat",
                MB_ICONERROR | MB_OK);
    return 1;
  }
  if (lock_result == ProfileLock::AcquireResult::kError) {
    MessageBoxW(nullptr, L"Error: Could not acquire profile lock.", L"AppTraverse Chat",
                MB_ICONERROR | MB_OK);
    return 1;
  }
  profile_lock_ = std::move(candidate);

  HINSTANCE const hinst = GetModuleHandleW(nullptr);
  CreateIpcNotifyWindow(hinst);
  class_registered_ = RegisterMainWindowClassOnce(hinst);

  main_hwnd_ = CreateWindowExW(0, kMainChatWindowClass, L"AppTraverse Chat", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, hinst,
                               this);
  if (main_hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW MainChatWindow", GetLastError());
  }

  ShowWindow(main_hwnd_, SW_SHOWNORMAL);
  UpdateWindow(main_hwnd_);

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

  if (!closing_) {
    session_.RequestStop();
  }
  session_.Join();
  ShutdownSessionAndResources();

  return static_cast<int>(msg.wParam);
}

}  // namespace apptraverse::example::chat_demo
