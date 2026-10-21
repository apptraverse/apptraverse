#if !defined(_WIN32)
int main() { return 0; }
#else

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <iostream>
#include <string>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_ui_native_push_button.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

constexpr wchar_t kParentClass[] = L"AppTraverseNativePushButtonTestParent";
constexpr std::uint32_t kControlId = 4242;

HWND CreateTestParent() {
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kParentClass;
    CHECK(RegisterClassW(&wc) != 0);
    registered = true;
  }
  HWND parent = CreateWindowExW(0, kParentClass, L"", WS_OVERLAPPEDWINDOW, 0,
                                0, 320, 240, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
  CHECK(parent != nullptr);
  return parent;
}

void TestHelperMatchesWinPushButtonCreateParams() {
  HWND parent = CreateTestParent();
  HWND btn = ui::windows::CreateChildPushButton({
      .parent_hwnd = parent,
      .control_id = kControlId,
      .initial_caption = L"Add",
      .x = 8,
      .y = 9,
      .width = 100,
      .height = 26,
      .presenter_user_data = reinterpret_cast<void*>(0xABC),
      .create_fatal_operation = "CreateWindowExW Ui push button",
  });
  CHECK(GetDlgCtrlID(btn) == static_cast<int>(kControlId));
  RECT rect{};
  CHECK(GetWindowRect(btn, &rect) != 0);
  CHECK(rect.right - rect.left == 100);
  CHECK(rect.bottom - rect.top == 26);
  wchar_t text[32]{};
  CHECK(GetWindowTextW(btn, text, 32) > 0);
  CHECK(std::wstring{text} == L"Add");
  CHECK(IsWindowEnabled(btn) != FALSE);
  EnableWindow(btn, FALSE);
  CHECK(IsWindowEnabled(btn) == FALSE);
  std::wstring const ru = ui::windows::Utf8ToWide("Добавить");
  SetWindowTextW(btn, ru.c_str());
  CHECK(GetWindowTextW(btn, text, 32) > 0);
  CHECK(std::wstring{text} == ru);

  ui::windows::DestroyChildPushButton(btn, "DestroyWindow Ui push button");
  CHECK(btn == nullptr);

  btn = ui::windows::CreateChildPushButton({
      .parent_hwnd = parent,
      .control_id = kControlId,
      .initial_caption = L"Again",
      .x = 11,
      .y = 21,
      .width = 121,
      .height = 29,
      .presenter_user_data = reinterpret_cast<void*>(0xDEF),
      .create_fatal_operation = "CreateWindowExW Ui push button",
  });
  CHECK(GetWindowTextW(btn, text, 32) > 0);
  CHECK(std::wstring{text} == L"Again");
  ui::windows::DestroyChildPushButton(btn, "DestroyWindow Ui push button");
  ui::windows::DestroyChildPushButton(btn, "DestroyWindow Ui push button");
  CHECK(btn == nullptr);
  DestroyWindow(parent);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestHelperMatchesWinPushButtonCreateParams();
  std::cout << "win_ui_native_push_button_test OK\n";
  return 0;
}

#endif
