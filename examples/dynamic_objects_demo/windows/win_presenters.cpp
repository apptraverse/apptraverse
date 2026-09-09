#include "win_presenters.h"

#include <cstdio>

#include "dynamic_ids.h"
#include "dynamic_lifecycle.h"
#include "dynamic_win32_messages.h"
#include "win32_fatal.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Win32MainWindowPresenter);
APPTRAVERSE_REGISTER(Win32ItemListPresenter);
APPTRAVERSE_REGISTER(Win32ItemPresenter);

void SetPresenterUserData(HWND hwnd, void* presenter) {
  SetLastError(0);
  LONG_PTR const previous = SetWindowLongPtrW(
      hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(presenter));
  if (previous == 0 && GetLastError() != 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowLongPtrW GWLP_USERDATA", err);
  }
}

}  // namespace

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
  if (msg == WM_COMMAND && LOWORD(wparam) == kAddButtonId) {
    HWND const notify = reinterpret_cast<HWND>(presenter->presentation_host);
    PostMessageW(notify, WM_APPTRAVERSE_ADD_ITEM, 0, 0);
    return 0;
  }
  if (msg == WM_CLOSE) {
    HWND const notify = reinterpret_cast<HWND>(presenter->presentation_host);
    if (PostMessageW(notify, WM_APPTRAVERSE_STOP, 0, 0) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("PostMessageW WM_APPTRAVERSE_STOP", err);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32MainWindowPresenter::OnLoad() {
  WNDCLASSW wc{};
  wc.lpfnWndProc = &Win32MainWindowPresenter::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kDynamicMainClass;
  if (RegisterClassW(&wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW DynamicMain", err);
  }
  hwnd = CreateWindowExW(0, kDynamicMainClass, kDynamicMainTitle,
                         WS_OVERLAPPEDWINDOW, window->x, window->y,
                         window->width, window->height, nullptr, nullptr,
                         GetModuleHandleW(nullptr), this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW DynamicMain", err);
  }
  add_button =
      CreateWindowExW(0, L"BUTTON", L"Add item",
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, 12, 100, 28,
                      hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddButtonId)),
                      GetModuleHandleW(nullptr), nullptr);
  if (add_button == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Add button", err);
  }
  SetPresenterUserData(hwnd, this);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

void Win32MainWindowPresenter::OnModelChanged() {}

void Win32MainWindowPresenter::OnUnload() {
  SetPresenterUserData(hwnd, nullptr);
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow DynamicMain", err);
  }
  hwnd = nullptr;
  add_button = nullptr;
  if (UnregisterClassW(kDynamicMainClass, GetModuleHandleW(nullptr)) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW DynamicMain", err);
  }
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
  if (msg == WM_COMMAND && LOWORD(wparam) == kRemoveButtonId) {
    HWND const button = reinterpret_cast<HWND>(lparam);
    auto const item_id =
        static_cast<ae::ObjId::Type>(GetWindowLongPtrW(button, GWLP_USERDATA));
    auto window = list_p->list->domain->Find(ae::ObjId{
        dynamic_objects::ToObjId(dynamic_objects::ObjId::MainWindow)});
    auto* main = dynamic_cast<MainWindow*>(&*window);
    auto* main_p =
        dynamic_cast<Win32MainWindowPresenter*>(&*main->presenter);
    HWND const notify = reinterpret_cast<HWND>(main_p->presentation_host);
    PostMessageW(notify, WM_APPTRAVERSE_REMOVE_ITEM,
                 static_cast<WPARAM>(item_id), 0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool Win32ItemListPresenter::ReadyForPresentation() const {
  if (!list.is_valid() || !list.is_loaded() || !list->domain) {
    return false;
  }
  auto window = list->domain->Find(
      ae::ObjId{dynamic_objects::ToObjId(dynamic_objects::ObjId::MainWindow)});
  if (!window) {
    return false;
  }
  auto* main = dynamic_cast<MainWindow*>(&*window);
  if (main == nullptr || !main->presenter.is_valid() ||
      !main->presenter.is_loaded()) {
    return false;
  }
  return main->presenter->presentation_loaded;
}

void Win32ItemListPresenter::OnLoad() {
  auto window = list->domain->Find(
      ae::ObjId{dynamic_objects::ToObjId(dynamic_objects::ObjId::MainWindow)});
  auto* main = dynamic_cast<MainWindow*>(&*window);
  auto* main_p =
      dynamic_cast<Win32MainWindowPresenter*>(&*main->presenter);
  assert(main_p != nullptr && main_p->hwnd != nullptr);

  WNDCLASSW wc{};
  wc.lpfnWndProc = &Win32ItemListPresenter::WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kDynamicItemListClass;
  if (RegisterClassW(&wc) == 0) {
    DWORD const err = GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      FatalWin32("RegisterClassW DynamicItemList", err);
    }
  }
  hwnd = CreateWindowExW(0, kDynamicItemListClass, L"", WS_CHILD | WS_VISIBLE,
                         12, 52, main->width - 40, main->height - 100,
                         main_p->hwnd, nullptr, GetModuleHandleW(nullptr),
                         this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW ItemList", err);
  }
}

void Win32ItemListPresenter::OnModelChanged() {}

void Win32ItemListPresenter::OnUnload() {
  if (hwnd == nullptr) {
    return;
  }
  // Parent Main may already have DestroyWindow'd this child during its OnUnload.
  if (IsWindow(hwnd) != 0) {
    SetPresenterUserData(hwnd, nullptr);
    if (DestroyWindow(hwnd) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("DestroyWindow ItemList", err);
    }
  }
  hwnd = nullptr;
}

bool Win32ItemPresenter::ReadyForPresentation() const {
  return item.is_valid() && item.is_loaded() && item->list.is_valid() &&
         item->list.is_loaded() && item->list->presenter.is_valid() &&
         item->list->presenter.is_loaded() &&
         item->list->presenter->presentation_loaded;
}

int Win32ItemPresenter::LiveIndex() const {
  assert(item.is_valid() && item->list.is_valid());
  int index = 0;
  for (auto const& entry : item->list->items) {
    if (&*entry == &*item) {
      return index;
    }
    ++index;
  }
  return 0;
}

void Win32ItemPresenter::OnLoad() {
  auto* list_p =
      dynamic_cast<Win32ItemListPresenter*>(&*item->list->presenter);
  assert(list_p != nullptr && list_p->hwnd != nullptr);
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
  SetWindowLongPtrW(remove_button, GWLP_USERDATA,
                    static_cast<LONG_PTR>(item->obj_id.id()));
}

void Win32ItemPresenter::OnModelChanged() {
  if (!item.is_valid()) {
    return;
  }
  int const index = LiveIndex();
  if (hwnd != nullptr) {
    wchar_t text[64];
    std::swprintf(text, 64, L"Item %u", item->number);
    SetWindowTextW(hwnd, text);
    SetWindowPos(hwnd, nullptr, 4, 4 + index * 28, 200, 22,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (remove_button != nullptr) {
    SetWindowPos(remove_button, nullptr, 220, 4 + index * 28, 28, 22,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

void Win32ItemPresenter::OnUnload() {
  // Parent ItemList/Main may already have destroyed these children.
  if (remove_button != nullptr) {
    if (IsWindow(remove_button) != 0) {
      if (DestroyWindow(remove_button) == 0) {
        DWORD const err = GetLastError();
        FatalWin32("DestroyWindow Item remove", err);
      }
    }
    remove_button = nullptr;
  }
  if (hwnd != nullptr) {
    if (IsWindow(hwnd) != 0) {
      if (DestroyWindow(hwnd) == 0) {
        DWORD const err = GetLastError();
        FatalWin32("DestroyWindow Item", err);
      }
    }
    hwnd = nullptr;
  }
}

}  // namespace apptraverse
