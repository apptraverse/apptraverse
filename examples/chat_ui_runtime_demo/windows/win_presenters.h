#ifndef APPTRAVERSE_CHAT_WIN_PRESENTERS_H_
#define APPTRAVERSE_CHAT_WIN_PRESENTERS_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "chat_commands.h"
#include "chat_ids.h"
#include "chat_log.h"
#include "chat_model.h"
#include "chat_presentation.h"
#include "chat_presence.h"
#include "ui_send_latency_tracker.h"
#include "win_connection_bar_presenter.h"
#include "win_util.h"

namespace apptraverse {

class WinChatWindowPresenter : public Presenter {
  APPTRAVERSE_OBJECT(WinChatWindowPresenter, Presenter, 0)

 protected:
  WinChatWindowPresenter() = default;

 public:
  explicit WinChatWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(room), AE_MMBR(identity), AE_MMBR(application))

  ChatRoom::ptr room;
  LocalAetherIdentity::ptr identity;
  Application::ptr application;
  std::function<void(ChatSendUiRequest)> on_send;
  std::function<void(std::string)> on_connect_host;
  std::function<void()> on_close;
  UiSendLatencyTracker* latency_tracker{nullptr};
  HWND hwnd{nullptr};
  HWND feed_hwnd{nullptr};
  HWND input_hwnd{nullptr};
  HWND contacts_hwnd{nullptr};
  HWND send_hwnd{nullptr};
  WinConnectionBarPresenter connection_bar;

  void OnLoad() override {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinChatWindowPresenter::WndProc;
    wc.hInstance = GetModuleModuleHandleSafe();
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    creating_ = true;
    hwnd = CreateWindowExW(
        0, kClassName, L"Chat", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        chat::kChatWindowLeft, chat::kChatWindowTop,
        chat::kChatWindowRight - chat::kChatWindowLeft,
        chat::kChatWindowBottom - chat::kChatWindowTop, nullptr, nullptr,
        GetModuleModuleHandleSafe(), this);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    creating_ = false;
    last_room_generation_ = room->Generation();
    last_identity_generation_ =
        identity.is_valid() ? identity->Generation() : 0;
    SyncClientGenerations();
    RebuildFromDomain();
  }

  void Present() {
    bool changed = false;
    if (room.is_valid() && room->Generation() != last_room_generation_) {
      last_room_generation_ = room->Generation();
      changed = true;
    }
    if (identity.is_valid() &&
        identity->Generation() != last_identity_generation_) {
      last_identity_generation_ = identity->Generation();
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
    connection_bar.Destroy();
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
        if (connection_bar.HandleCommand(wparam)) {
          return 0;
        }
        return DefWindowProcW(wnd, msg, wparam, lparam);
      }
      case WM_CLOSE:
        if (on_close) {
          on_close();
        }
        return 0;
      case WM_DESTROY:
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
    connection_bar.on_connect_host = on_connect_host;
    connection_bar.Create(wnd, role, inst);
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
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 0, 0, 0, 0,
        wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdContacts)), inst,
        nullptr);
    send_hwnd = CreateWindowExW(
        0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0,
        0, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSend)), inst,
        nullptr);
    SetWindowLongPtrW(input_hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    input_prev_proc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        input_hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&WinChatWindowPresenter::InputEditProc)));
    LayoutNative(client.right - client.left, client.bottom - client.top);
  }

  void LayoutNative(int client_w, int client_h) {
    int const gap = 8;
    int const bar_h = chat::kChatConnectionBarHeight;
    int const side = chat::kChatSidebarWidth;
    int const input_h = chat::kChatInputHeight;
    int const body_top = bar_h + gap;
    int const body_h = client_h - body_top;
    int const main_w = client_w - side - gap * 3;
    if (main_w < 40 || body_h < input_h + gap * 2) {
      return;
    }
    connection_bar.Layout(client_w, 0, bar_h);
    int const feed_h = body_h - input_h - gap * 2;
    MoveWindow(feed_hwnd, gap, body_top + gap, main_w, feed_h, TRUE);
    MoveWindow(input_hwnd, gap, body_top + gap + feed_h + gap, main_w, input_h,
               TRUE);
    int const side_x = gap * 2 + main_w;
    int const send_y = client_h - input_h - gap;
    int const contacts_h = send_y - body_top - gap * 2;
    if (contacts_h > 20) {
      MoveWindow(contacts_hwnd, side_x, body_top + gap, side, contacts_h,
                 TRUE);
    }
    MoveWindow(send_hwnd, side_x, send_y, side, input_h, TRUE);
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
    if (connection_bar.role() == ChatRole::Host) {
      connection_bar.UpdateFromDomain(identity);
    }
    if (feed_hwnd == nullptr || contacts_hwnd == nullptr || !room.is_valid()) {
      return;
    }
    ChatPresentationOptions options;
    if (identity.is_valid()) {
      options.local_aether_uid = identity->UidTextBytes();
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
          chat::ChatLog("UI_MESSAGE_PRESENTED event_obj_id=" +
                        std::to_string(event_obj_id) + " latency_us=" +
                        std::to_string(static_cast<std::int64_t>(
                            (*resolved) * 1000.0)));
        }
        return resolved;
      };
    }
    auto snapshot = BuildChatPresentationSnapshot(*room, options);
    SendMessageW(feed_hwnd, LB_RESETCONTENT, 0, 0);
    for (auto const& item : snapshot.feed) {
      auto wide = Utf8ToWide(item.display_line);
      SendMessageW(feed_hwnd, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(wide.c_str()));
    }
    int const count =
        static_cast<int>(SendMessageW(feed_hwnd, LB_GETCOUNT, 0, 0));
    if (count > 0) {
      SendMessageW(feed_hwnd, LB_SETTOPINDEX, count - 1, 0);
    }

    SendMessageW(contacts_hwnd, LB_RESETCONTENT, 0, 0);
    for (auto const& contact : snapshot.contacts) {
      auto wide = FormatContactPresenceLabel(
          contact.online, Utf8ToWide(contact.display_name));
      SendMessageW(contacts_hwnd, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(wide.c_str()));
    }
  }

  WNDPROC input_prev_proc_{nullptr};
  std::uint64_t last_room_generation_{0};
  std::uint64_t last_identity_generation_{0};
  std::unordered_map<std::uint32_t, std::uint64_t> last_client_generations_;
  bool creating_{false};

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
  APPTRAVERSE_OBJECT(WinChatPresentationApplication, Presenter, 0)

 protected:
  WinChatPresentationApplication() = default;

 public:
  explicit WinChatPresentationApplication(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat_window))

  WinChatWindowPresenter::ptr chat_window;
  std::function<void()> on_close;
  std::function<void(ChatSendUiRequest)> on_chat_send;
  std::function<void(std::string)> on_connect_host;
  UiSendLatencyTracker* latency_tracker{nullptr};

  void OnLoad() override {
    chat_window->on_close = on_close;
    chat_window->on_send = on_chat_send;
    chat_window->on_connect_host = on_connect_host;
    chat_window->latency_tracker = latency_tracker;
    chat_window->OnLoad();
  }

  void PresentChatWindow() { chat_window->Present(); }

  void Destroy() { chat_window->Destroy(); }
};

WinChatPresentationApplication::ptr BuildPresentationGraph(
    ae::Domain& domain, Application& application);

void EnsureChatPresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_WIN_PRESENTERS_H_
