#ifndef APPTRAVERSE_CHAT_WIN_PRESENTERS_H_
#define APPTRAVERSE_CHAT_WIN_PRESENTERS_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "chat_commands.h"
#include "chat_ids.h"
#include "chat_log.h"
#include "chat_model.h"
#include "chat_presentation.h"
#include "chat_presence.h"
#include "chat_runtime_diag_ui_state.h"
#include "ui_send_latency_tracker.h"
#include "win_identity_bar_presenter.h"
#include "win_util.h"

namespace chat::win32 {
using apptraverse::Presenter;

class WinChatWindowPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("chat::win32::WinChatWindowPresenter", WinChatWindowPresenter, Presenter, 0)

 protected:
  WinChatWindowPresenter() = default;

 public:
  explicit WinChatWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(room), AE_MMBR(network), AE_MMBR(aether),
                    AE_MMBR(application))

  ChatRoom::ptr room;
  NetworkState::ptr network;
  AetherRegistrationState::ptr aether;
  ChatApplication::ptr application;
  std::function<void(ChatSendUiRequest)> on_send;
  std::function<void()> on_join_room;
  std::function<void()> on_close;
  UiSendLatencyTracker* latency_tracker{nullptr};
  HWND hwnd{nullptr};
  HWND feed_hwnd{nullptr};
  HWND input_hwnd{nullptr};
  HWND contacts_hwnd{nullptr};
  HWND send_hwnd{nullptr};
  HWND diag_hwnd_{nullptr};
  WinIdentityBarPresenter identity_bar;

  void OnLoad() override {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinChatWindowPresenter::WndProc;
    wc.hInstance = GetModuleModuleHandleSafe();
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    ChatRole const role =
        application.is_valid() ? application->GetRole() : ChatRole::Host;
    wchar_t const* title = role == ChatRole::Host
                               ? L"AppTraverse Chat — Host"
                               : L"AppTraverse Chat — Client";

    creating_ = true;
    hwnd = CreateWindowExW(
        0, kClassName, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        kChatWindowLeft, kChatWindowTop,
        kChatWindowRight - kChatWindowLeft,
        kChatWindowBottom - kChatWindowTop, nullptr, nullptr,
        GetModuleModuleHandleSafe(), this);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    creating_ = false;
    last_room_generation_ = room->Generation();
    last_network_generation_ =
        network.is_valid() ? network->Generation() : 0;
    last_aether_generation_ = aether.is_valid() ? aether->Generation() : 0;
    SyncClientGenerations();
    RebuildFromDomain();
  }

  void NotifyPeerReady() {}

  void NotifyPeerDisconnected() {}

  void ApplyRuntimeDiag(ChatRuntimeDiagUiState const& diag) {
#ifndef NDEBUG
    runtime_diag_ = diag;
    RefreshDiagLabel();
#else
    (void)diag;
#endif
  }

  void Present() {
    bool changed = false;
    if (room.is_valid() && room->Generation() != last_room_generation_) {
      last_room_generation_ = room->Generation();
      changed = true;
    }
    if (network.is_valid() &&
        network->Generation() != last_network_generation_) {
      last_network_generation_ = network->Generation();
      changed = true;
    }
    if (aether.is_valid() &&
        aether->Generation() != last_aether_generation_) {
      last_aether_generation_ = aether->Generation();
      changed = true;
    }
    if (SyncClientGenerations()) {
      changed = true;
    }
    if (changed) {
      RebuildFromDomain();
    }
  }

  void Destroy() {
    HWND h = hwnd;
    hwnd = nullptr;
    feed_hwnd = nullptr;
    input_hwnd = nullptr;
    contacts_hwnd = nullptr;
    send_hwnd = nullptr;
    diag_hwnd_ = nullptr;
    identity_bar.Destroy();
    if (h != nullptr) {
      DestroyWindow(h);
    }
  }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseChatWindow";
  static constexpr int kIdFeed = 101;
  static constexpr int kIdInput = 102;
  static constexpr int kIdContacts = 103;
  static constexpr int kIdSend = 104;

  static HINSTANCE GetModuleModuleHandleSafe() {
    return GetModuleHandleW(nullptr);
  }

  static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self = static_cast<WinChatWindowPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd = wnd;
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinChatWindowPresenter*>(
        GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    return self->Handle(wnd, msg, wparam, lparam);
  }

  LRESULT Handle(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_CREATE:
        OnCreate(wnd);
        return 0;
      case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
          LayoutNative(LOWORD(lparam), HIWORD(lparam));
        }
        return 0;
      case WM_COMMAND: {
        if (HIWORD(wparam) == BN_CLICKED &&
            LOWORD(wparam) == static_cast<WORD>(kIdSend)) {
          TrySend();
          return 0;
        }
        if (identity_bar.HandleCommand(wparam)) {
          return 0;
        }
        return DefWindowProcW(wnd, msg, wparam, lparam);
      }
      case WM_CLOSE:
        if (on_close) {
          on_close();
        }
        return 0;
      case WM_MEASUREITEM:
        if (reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->CtlID ==
            static_cast<UINT>(kIdContacts)) {
          reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight =
              contact_row_height_;
          return TRUE;
        }
        return DefWindowProcW(wnd, msg, wparam, lparam);
      case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw->CtlID == static_cast<UINT>(kIdContacts)) {
          DrawContactRow(*draw);
          return TRUE;
        }
        return DefWindowProcW(wnd, msg, wparam, lparam);
      }
      case WM_DESTROY:
        ReleaseContactFonts();
        hwnd = nullptr;
        return 0;
      default:
        return DefWindowProcW(wnd, msg, wparam, lparam);
    }
  }

  static LRESULT CALLBACK InputEditProc(HWND wnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam) {
    auto* self = reinterpret_cast<WinChatWindowPresenter*>(
        GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    if (msg == WM_KEYDOWN && wparam == VK_RETURN &&
        (GetKeyState(VK_SHIFT) & 0x8000) == 0) {
      self->TrySend();
      return 0;
    }
    return CallWindowProcW(self->input_prev_proc_, wnd, msg, wparam, lparam);
  }

  void OnCreate(HWND wnd) {
    RECT client{};
    GetClientRect(wnd, &client);
    HINSTANCE inst = GetModuleModuleHandleSafe();
    ChatRole const role = application.is_valid() ? application->GetRole()
                                                 : ChatRole::Host;
    identity_bar.on_join_room = on_join_room;
    identity_bar.Create(wnd, inst, role);
    feed_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
        0, 0, 0, 0, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFeed)),
        inst, nullptr);
    input_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0, 0, wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdInput)), inst, nullptr);
    contacts_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        0, 0, 0, 0, wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdContacts)), inst,
        nullptr);
    EnsureContactFonts();
    send_hwnd = CreateWindowExW(
        0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0,
        0, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSend)), inst,
        nullptr);
#ifndef NDEBUG
    diag_hwnd_ = CreateWindowExW(
        0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, wnd,
        nullptr, inst, nullptr);
#endif
    SetWindowLongPtrW(input_hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    input_prev_proc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        input_hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&WinChatWindowPresenter::InputEditProc)));
    LayoutNative(client.right - client.left, client.bottom - client.top);
  }

  void LayoutNative(int client_w, int client_h) {
    int const gap = 8;
    int const bar_h = kChatConnectionBarHeight;
#ifndef NDEBUG
    int const diag_h = 18;
#else
    int const diag_h = 0;
#endif
    int const side = kChatSidebarWidth;
    int const input_h = kChatInputHeight;
    int const body_top = bar_h + gap;
    int const body_h = client_h - body_top - diag_h;
    int const main_w = client_w - side - gap * 3;
    if (main_w < 40 || body_h < input_h + gap * 2) {
      return;
    }
    identity_bar.Layout(client_w, 0, bar_h);
    int const feed_h = body_h - input_h - gap * 2;
    MoveWindow(feed_hwnd, gap, body_top + gap, main_w, feed_h, TRUE);
    MoveWindow(input_hwnd, gap, body_top + gap + feed_h + gap, main_w, input_h,
               TRUE);
    int const side_x = gap * 2 + main_w;
    int const send_y = body_top + body_h - input_h - gap;
    int const contacts_h = send_y - body_top - gap * 2;
    if (contacts_h > 20) {
      MoveWindow(contacts_hwnd, side_x, body_top + gap, side, contacts_h,
                 TRUE);
    }
    MoveWindow(send_hwnd, side_x, send_y, side, input_h, TRUE);
#ifndef NDEBUG
    if (diag_hwnd_ != nullptr && diag_h > 0) {
      MoveWindow(diag_hwnd_, gap, client_h - diag_h, client_w - gap * 2,
                 diag_h, TRUE);
    }
#endif
  }

  void TrySend() {
    if (!on_send || input_hwnd == nullptr) {
      return;
    }
    auto text = ReadEditTextUtf8(input_hwnd);
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
      return;
    }
    auto const sent_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    ChatSendUiRequest request;
    request.text = std::move(text);
    request.sent_at_unix_ms = sent_at;
    if (latency_tracker != nullptr) {
      request.ui_trace_id =
          latency_tracker->Begin(std::chrono::steady_clock::now());
    }
    on_send(std::move(request));
    SetWindowTextW(input_hwnd, L"");
  }

  void RebuildFromDomain() {
    ChatRole const role =
        application.is_valid() ? application->GetRole() : ChatRole::Host;
    identity_bar.UpdateFromDomain(network, aether, role);
    if (feed_hwnd == nullptr || contacts_hwnd == nullptr || !room.is_valid()) {
      return;
    }
    ChatPresentationOptions options;
    if (aether.is_valid()) {
      options.local_aether_uid = aether->CurrentUid();
    }
    if (latency_tracker != nullptr) {
      options.latency_ms_for_event =
          [this](std::uint32_t event_obj_id) -> std::optional<double> {
        if (auto cached = latency_tracker->CachedLatencyMs(event_obj_id)) {
          return cached;
        }
        auto resolved = latency_tracker->ResolveForPresentation(
            event_obj_id, std::chrono::steady_clock::now());
        if (resolved) {
          ChatLog("UI_MESSAGE_PRESENTED event_obj_id=" +
                        std::to_string(event_obj_id) + " latency_us=" +
                        std::to_string(static_cast<std::int64_t>(
                            (*resolved) * 1000.0)));
        }
        return resolved;
      };
    }
    auto snapshot = BuildChatPresentationSnapshot(*room, options);

    int const prev_count =
        static_cast<int>(SendMessageW(feed_hwnd, LB_GETCOUNT, 0, 0));
    int const prev_top =
        static_cast<int>(SendMessageW(feed_hwnd, LB_GETTOPINDEX, 0, 0));
    int visible_items = 1;
    if (prev_count > 0) {
      RECT feed_rect{};
      GetClientRect(feed_hwnd, &feed_rect);
      int const item_h = static_cast<int>(
          SendMessageW(feed_hwnd, LB_GETITEMHEIGHT, 0, 0));
      if (item_h > 0) {
        visible_items =
            (feed_rect.bottom - feed_rect.top) / item_h;
        if (visible_items < 1) {
          visible_items = 1;
        }
      }
    }
    bool const was_at_bottom =
        FeedListWasAtBottom(prev_top, visible_items, prev_count);

    SendMessageW(feed_hwnd, LB_RESETCONTENT, 0, 0);
    for (auto const& item : snapshot.feed) {
      auto wide = Utf8ToWide(item.display_line);
      SendMessageW(feed_hwnd, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(wide.c_str()));
    }
    int const count =
        static_cast<int>(SendMessageW(feed_hwnd, LB_GETCOUNT, 0, 0));
    if (count > 0) {
      if (was_at_bottom) {
        SendMessageW(feed_hwnd, LB_SETTOPINDEX, count - 1, 0);
      } else {
        int top = prev_top;
        if (top < 0) {
          top = 0;
        }
        if (top > count - 1) {
          top = count - 1;
        }
        SendMessageW(feed_hwnd, LB_SETTOPINDEX, top, 0);
      }
    }

    SendMessageW(contacts_hwnd, LB_RESETCONTENT, 0, 0);
    contact_rows_.clear();
    for (auto const& contact : snapshot.contacts) {
      ContactRow row;
      row.text = FormatContactPresenceLabel(
          contact.presence, Utf8ToWide(contact.display_name));
      row.is_local = contact.is_local;
      contact_rows_.push_back(std::move(row));
      SendMessageW(contacts_hwnd, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(contact_rows_.back().text.c_str()));
    }
#ifndef NDEBUG
    runtime_diag_.room_journal_count = room->journal.size();
    runtime_diag_.client_journal_count = 0;
    if (application.is_valid() && application->local_client.is_valid()) {
      application->local_client.Load();
      runtime_diag_.client_journal_count =
          application->local_client->journal.size();
    }
    RefreshDiagLabel();
#endif
  }

  void RefreshDiagLabel() {
#ifndef NDEBUG
    if (diag_hwnd_ == nullptr) {
      return;
    }
    std::ostringstream out;
    out << "Room journal: " << runtime_diag_.room_journal_count
        << "  Client journal: " << runtime_diag_.client_journal_count;
    auto wide = Utf8ToWide(out.str());
    SetWindowTextW(diag_hwnd_, wide.c_str());
#else
#endif
  }

  struct ContactRow {
    std::wstring text;
    bool is_local{false};
  };

  void EnsureContactFonts() {
    if (contact_normal_font_ != nullptr || contacts_hwnd == nullptr) {
      return;
    }
    LOGFONTW lf{};
    HFONT control_font =
        reinterpret_cast<HFONT>(SendMessageW(contacts_hwnd, WM_GETFONT, 0, 0));
    if (control_font != nullptr &&
        GetObjectW(control_font, sizeof(lf), &lf) != 0) {
      // Use the control font as the normal face.
    } else {
      NONCLIENTMETRICSW metrics{};
      metrics.cbSize = sizeof(metrics);
      SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                            0);
      lf = metrics.lfMessageFont;
    }
    contact_normal_font_ = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_BOLD;
    contact_bold_font_ = CreateFontIndirectW(&lf);
    HDC dc = GetDC(hwnd);
    if (dc != nullptr) {
      HFONT old = static_cast<HFONT>(SelectObject(dc, contact_normal_font_));
      TEXTMETRICW tm{};
      GetTextMetricsW(dc, &tm);
      contact_row_height_ = tm.tmHeight + tm.tmExternalLeading + 4;
      SelectObject(dc, old);
      ReleaseDC(hwnd, dc);
    } else {
      contact_row_height_ = 18;
    }
  }

  void ReleaseContactFonts() {
    if (contact_normal_font_ != nullptr) {
      DeleteObject(contact_normal_font_);
      contact_normal_font_ = nullptr;
    }
    if (contact_bold_font_ != nullptr) {
      DeleteObject(contact_bold_font_);
      contact_bold_font_ = nullptr;
    }
  }

  void DrawContactRow(DRAWITEMSTRUCT const& draw) {
    if (draw.itemID == static_cast<UINT>(-1) ||
        draw.itemID >= contact_rows_.size()) {
      return;
    }
    EnsureContactFonts();
    auto const& row = contact_rows_[draw.itemID];
    COLORREF bg = (draw.itemState & ODS_SELECTED) != 0
                      ? GetSysColor(COLOR_HIGHLIGHT)
                      : GetSysColor(COLOR_WINDOW);
    COLORREF fg = (draw.itemState & ODS_SELECTED) != 0
                      ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                      : GetSysColor(COLOR_WINDOWTEXT);
    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(draw.hDC, &draw.rcItem, brush);
    DeleteObject(brush);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, fg);
    HFONT font =
        row.is_local ? contact_bold_font_ : contact_normal_font_;
    HFONT old = static_cast<HFONT>(SelectObject(draw.hDC, font));
    RECT text_rect = draw.rcItem;
    text_rect.left += 4;
    DrawTextW(draw.hDC, row.text.c_str(), -1, &text_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(draw.hDC, old);
    if ((draw.itemState & ODS_FOCUS) != 0) {
      DrawFocusRect(draw.hDC, &draw.rcItem);
    }
  }

  WNDPROC input_prev_proc_{nullptr};
  std::uint64_t last_room_generation_{0};
  std::uint64_t last_network_generation_{0};
  std::uint64_t last_aether_generation_{0};
  std::unordered_map<std::uint32_t, std::uint64_t> last_client_generations_;
  std::vector<ContactRow> contact_rows_;
  HFONT contact_normal_font_{nullptr};
  HFONT contact_bold_font_{nullptr};
  int contact_row_height_{18};
  bool creating_{false};
  ChatRuntimeDiagUiState runtime_diag_{};

  bool SyncClientGenerations() {
    if (!room.is_valid()) {
      return false;
    }
    bool changed = false;
    for (auto const& client : room->clients) {
      if (!client.is_valid()) {
        continue;
      }
      client.Load();
      auto const id = client.id().id();
      auto const generation = client->Generation();
      auto const tracked = last_client_generations_.find(id);
      if (tracked == last_client_generations_.end() ||
          tracked->second != generation) {
        last_client_generations_[id] = generation;
        changed = true;
      }
    }
    return changed;
  }
};

class WinChatPresentationApplication : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("chat::win32::WinChatPresentationApplication", WinChatPresentationApplication, Presenter, 0)

 protected:
  WinChatPresentationApplication() = default;

 public:
  explicit WinChatPresentationApplication(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat_window))

  WinChatWindowPresenter::ptr chat_window;
  std::function<void()> on_close;
  std::function<void(ChatSendUiRequest)> on_chat_send;
  std::function<void()> on_join_room;
  UiSendLatencyTracker* latency_tracker{nullptr};

  void OnLoad() override {
    chat_window->on_close = on_close;
    chat_window->on_send = on_chat_send;
    chat_window->on_join_room = on_join_room;
    chat_window->latency_tracker = latency_tracker;
    chat_window->OnLoad();
  }

  void PresentChatWindow() { chat_window->Present(); }

  void NotifyPeerReady() {
    if (chat_window.is_valid()) {
      chat_window->NotifyPeerReady();
    }
  }

  void NotifyPeerDisconnected() {
    if (chat_window.is_valid()) {
      chat_window->NotifyPeerDisconnected();
    }
  }

  void ApplyRuntimeDiag(ChatRuntimeDiagUiState const& diag) {
    if (chat_window.is_valid()) {
      chat_window->ApplyRuntimeDiag(diag);
    }
  }

  void Destroy() { chat_window->Destroy(); }
};

WinChatPresentationApplication::ptr BuildPresentationGraph(
    ae::Domain& domain, ChatApplication& application);

void EnsureChatPresenterRegistration();

}  // namespace chat::win32

#endif  // APPTRAVERSE_CHAT_WIN_PRESENTERS_H_
