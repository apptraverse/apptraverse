#include "win_presenters.h"

#include <cassert>
#include <cstdio>

#include "dynamic_win32_messages.h"
#include "win32_fatal.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Win32MainWindowPresenter);
APPTRAVERSE_REGISTER(Win32AddItemPresenter);
APPTRAVERSE_REGISTER(Win32ItemListPresenter);
APPTRAVERSE_REGISTER(Win32ItemPresenter);

void SetHwndUserData(HWND hwnd, void* value) {
  SetLastError(0);
  LONG_PTR const previous =
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
  if (previous == 0 && GetLastError() != 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowLongPtrW GWLP_USERDATA", err);
  }
}

}  // namespace

bool DispatchChildCommand(WPARAM wparam, LPARAM lparam) {
  HWND const child = reinterpret_cast<HWND>(lparam);
  if (child == nullptr) {
    return false;
  }
  auto* owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(child, GWLP_USERDATA));
  if (owner == nullptr) {
    return false;
  }
  return owner->OnCommand(static_cast<std::uint32_t>(LOWORD(wparam)),
                          static_cast<std::uint16_t>(HIWORD(wparam)));
}

void RegisterDynamicWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW main_wc{};
  main_wc.lpfnWndProc = &Win32MainWindowPresenter::WndProc;
  main_wc.hInstance = instance;
  main_wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  main_wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  main_wc.lpszClassName = kDynamicMainClass;
  if (RegisterClassW(&main_wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW DynamicMain", err);
  }

  WNDCLASSW list_wc{};
  list_wc.lpfnWndProc = &Win32ItemListPresenter::WndProc;
  list_wc.hInstance = instance;
  list_wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  list_wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  list_wc.lpszClassName = kDynamicItemListClass;
  if (RegisterClassW(&list_wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW DynamicItemList", err);
  }
}

void UnregisterDynamicWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  if (UnregisterClassW(kDynamicItemListClass, instance) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW DynamicItemList", err);
  }
  if (UnregisterClassW(kDynamicMainClass, instance) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW DynamicMain", err);
  }
}

LRESULT CALLBACK Win32MainWindowPresenter::WndProc(HWND hwnd, UINT msg,
                                                   WPARAM wparam,
                                                   LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* presenter = reinterpret_cast<Win32MainWindowPresenter*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (presenter == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_COMMAND && DispatchChildCommand(wparam, lparam)) {
    return 0;
  }
  if (msg == WM_CLOSE) {
    HWND const notify = reinterpret_cast<HWND>(presenter->presentation_host);
    PostMessageW(notify, WM_APPTRAVERSE_CLOSE_WINDOW,
                 static_cast<WPARAM>(presenter->window->obj_id.id()), 0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32MainWindowPresenter::OnLoad() {
  hwnd = CreateWindowExW(0, kDynamicMainClass, kDynamicMainTitle,
                         WS_OVERLAPPEDWINDOW, window->x, window->y,
                         window->width, window->height, nullptr, nullptr,
                         GetModuleHandleW(nullptr), this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW DynamicMain", err);
  }
  SetHwndUserData(hwnd, this);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

void Win32MainWindowPresenter::OnModelChanged() {}

void Win32MainWindowPresenter::OnUnload() {
  SetHwndUserData(hwnd, nullptr);
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow DynamicMain", err);
  }
  hwnd = nullptr;
}

bool Win32AddItemPresenter::ReadyForPresentation() const {
  return add_item->window->presenter->presentation_loaded;
}

void Win32AddItemPresenter::OnLoad() {
  Win32MainWindowPresenter::ptr main_p{add_item->window->presenter};
  assert(main_p->hwnd != nullptr);
  hwnd = CreateWindowExW(
      0, L"BUTTON", L"Add item", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 12,
      100, 28, main_p->hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddButtonId)),
      GetModuleHandleW(nullptr), nullptr);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Add item", err);
  }
  SetHwndUserData(hwnd, static_cast<Presenter*>(this));
}

void Win32AddItemPresenter::OnModelChanged() {}

void Win32AddItemPresenter::OnUnload() {
  SetHwndUserData(hwnd, nullptr);
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Add item", err);
  }
  hwnd = nullptr;
}

bool Win32AddItemPresenter::OnCommand(std::uint32_t command_id,
                                      std::uint16_t notification_code) {
  (void)command_id;
  if (notification_code != BN_CLICKED) {
    return false;
  }
  Click();
  return true;
}

LRESULT CALLBACK Win32ItemListPresenter::WndProc(HWND hwnd, UINT msg,
                                                 WPARAM wparam,
                                                 LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* list_p = reinterpret_cast<Win32ItemListPresenter*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (list_p == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_COMMAND && DispatchChildCommand(wparam, lparam)) {
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool Win32ItemListPresenter::ReadyForPresentation() const {
  return list->window->presenter->presentation_loaded;
}

void Win32ItemListPresenter::OnLoad() {
  Win32MainWindowPresenter::ptr main_p{list->window->presenter};
  assert(main_p->hwnd != nullptr);

  hwnd = CreateWindowExW(0, kDynamicItemListClass, L"", WS_CHILD | WS_VISIBLE,
                         12, 52, list->window->width - 40,
                         list->window->height - 100, main_p->hwnd, nullptr,
                         GetModuleHandleW(nullptr), this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW ItemList", err);
  }
}

void Win32ItemListPresenter::OnModelChanged() {}

void Win32ItemListPresenter::OnUnload() {
  SetHwndUserData(hwnd, nullptr);
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow ItemList", err);
  }
  hwnd = nullptr;
}

bool Win32ItemPresenter::ReadyForPresentation() const {
  return item->list->presenter->presentation_loaded;
}

int Win32ItemPresenter::LiveIndex() const {
  int index = 0;
  for (auto const& entry : item->list->items) {
    if (&*entry == &*item) {
      return index;
    }
    ++index;
  }
  assert(false && "Item must be live in its ItemList for presentation");
  return 0;
}

void Win32ItemPresenter::OnLoad() {
  Win32ItemListPresenter::ptr list_p{item->list->presenter};
  assert(list_p->hwnd != nullptr);
  wchar_t text[64];
  std::swprintf(text, 64, L"Item %u", item->number);
  int const index = LiveIndex();
  hwnd = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, 4,
                         4 + index * 28, 200, 22, list_p->hwnd, nullptr,
                         GetModuleHandleW(nullptr), nullptr);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Item", err);
  }
  remove_button = CreateWindowExW(
      0, L"BUTTON", L"x", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 220,
      4 + index * 28, 28, 22, list_p->hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRemoveButtonId)),
      GetModuleHandleW(nullptr), nullptr);
  if (remove_button == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Remove button", err);
  }
  SetHwndUserData(remove_button, static_cast<Presenter*>(this));
}

void Win32ItemPresenter::OnModelChanged() {
  int const index = LiveIndex();
  wchar_t text[64];
  std::swprintf(text, 64, L"Item %u", item->number);
  SetWindowTextW(hwnd, text);
  SetWindowPos(hwnd, nullptr, 4, 4 + index * 28, 200, 22,
               SWP_NOZORDER | SWP_NOACTIVATE);
  SetWindowPos(remove_button, nullptr, 220, 4 + index * 28, 28, 22,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void Win32ItemPresenter::OnUnload() {
  SetHwndUserData(remove_button, nullptr);
  if (DestroyWindow(remove_button) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Item remove", err);
  }
  remove_button = nullptr;
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Item", err);
  }
  hwnd = nullptr;
}

bool Win32ItemPresenter::OnCommand(std::uint32_t command_id,
                                   std::uint16_t notification_code) {
  (void)command_id;
  if (notification_code != BN_CLICKED) {
    return false;
  }
  RemoveClick();
  return true;
}

}  // namespace apptraverse
