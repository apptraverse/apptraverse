#ifndef APPTRAVERSE_CHAT_WIN_PRESENTERS_H_
#define APPTRAVERSE_CHAT_WIN_PRESENTERS_H_

#include <functional>
#include <string>

#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "chat_commands.h"
#include "chat_ids.h"
#include "chat_model.h"
#include "win_util.h"

namespace apptraverse {

class WinChatWindowPresenter : public Presenter {
  APPTRAVERSE_OBJECT(WinChatWindowPresenter, Presenter, 0)

 protected:
  WinChatWindowPresenter() = default;

 public:
  explicit WinChatWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(room), AE_MMBR(identity))

  ChatRoom::ptr room;
  LocalAetherIdentity::ptr identity;
  std::function<void(std::string)> on_send;
  std::function<void()> on_close;
  HWND hwnd{nullptr};
  HWND feed_hwnd{nullptr};
  HWND input_hwnd{nullptr};
  HWND contacts_hwnd{nullptr};
  HWND send_hwnd{nullptr};
  HWND aether_label_hwnd{nullptr};
  HWND aether_uid_hwnd{nullptr};

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
    aether_label_hwnd = nullptr;
    aether_uid_hwnd = nullptr;
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
  static constexpr int kIdAetherUid = 105;

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
    feed_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
        0, 0, 0, 0, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdFeed)),
        inst, nullptr);
    input_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0, 0, wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdInput)), inst, nullptr);
    aether_label_hwnd = CreateWindowExW(
        0, L"STATIC", L"Your Aether ID:", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0,
        0, 0, wnd, nullptr, inst, nullptr);
    aether_uid_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"...",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT | ES_READONLY, 0, 0, 0,
        0, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAetherUid)),
        inst, nullptr);
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
    int const side = chat::kChatSidebarWidth;
    int const input_h = chat::kChatInputHeight;
    int const aether_h = chat::kChatAetherIdBlockHeight;
    int const gap = 8;
    int const main_w = client_w - side - gap * 3;
    if (main_w < 40 || client_h < 80) {
      return;
    }
    int const feed_h = client_h - input_h - gap * 3;
    MoveWindow(feed_hwnd, gap, gap, main_w, feed_h, TRUE);
    MoveWindow(input_hwnd, gap, gap * 2 + feed_h, main_w, input_h, TRUE);
    int const side_x = gap * 2 + main_w;
    int const label_h = 18;
    int const uid_h = 24;
    MoveWindow(aether_label_hwnd, side_x, gap, side, label_h, TRUE);
    MoveWindow(aether_uid_hwnd, side_x, gap + label_h, side, uid_h, TRUE);
    int const contacts_top = gap + aether_h;
    int const send_y = client_h - input_h - gap;
    int const contacts_h = send_y - contacts_top - gap;
    if (contacts_h > 20) {
      MoveWindow(contacts_hwnd, side_x, contacts_top, side, contacts_h, TRUE);
    }
    MoveWindow(send_hwnd, side_x, send_y, side, input_h, TRUE);
  }

  void TrySend() {
    if (!on_send || input_hwnd == nullptr) {
      return;
    }
    int const n = GetWindowTextLengthW(input_hwnd);
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    if (n > 0) {
      GetWindowTextW(input_hwnd, wide.data(), n + 1);
    }
    std::string text;
    if (!wide.empty()) {
      int const bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                            static_cast<int>(wide.size()),
                                            nullptr, 0, nullptr, nullptr);
      text.resize(static_cast<std::size_t>(bytes));
      WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                          static_cast<int>(wide.size()), text.data(), bytes,
                          nullptr, nullptr);
    }
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
      return;
    }
    on_send(std::move(text));
    SetWindowTextW(input_hwnd, L"");
  }

  void RebuildFromDomain() {
    if (aether_uid_hwnd != nullptr && identity.is_valid()) {
      auto wide = Utf8ToWide(identity->UidTextBytes());
      SetWindowTextW(aether_uid_hwnd, wide.c_str());
    }
    if (feed_hwnd == nullptr || contacts_hwnd == nullptr || !room.is_valid()) {
      return;
    }
    SendMessageW(feed_hwnd, LB_RESETCONTENT, 0, 0);
    for (auto const& item : room->feed) {
      if (!item.is_valid()) {
        continue;
      }
      item.Load();
      auto line = FormatChatFeedLine(*item);
      auto wide = Utf8ToWide(line);
      SendMessageW(feed_hwnd, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(wide.c_str()));
    }
    int const count =
        static_cast<int>(SendMessageW(feed_hwnd, LB_GETCOUNT, 0, 0));
    if (count > 0) {
      SendMessageW(feed_hwnd, LB_SETTOPINDEX, count - 1, 0);
    }

    SendMessageW(contacts_hwnd, LB_RESETCONTENT, 0, 0);
    for (auto const& client : room->clients) {
      if (!client.is_valid()) {
        continue;
      }
      client.Load();
      auto wide = Utf8ToWide(client->DisplayNameBytes());
      SendMessageW(contacts_hwnd, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(wide.c_str()));
    }
  }

  WNDPROC input_prev_proc_{nullptr};
  std::uint64_t last_room_generation_{0};
  std::uint64_t last_identity_generation_{0};
  bool creating_{false};
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
  std::function<void(std::string)> on_chat_send;

  void OnLoad() override {
    chat_window->on_close = on_close;
    chat_window->on_send = on_chat_send;
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
