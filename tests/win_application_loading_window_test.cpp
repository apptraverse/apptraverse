#include <cstdlib>
#include <iostream>
#include <string>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_application_loading_window.h"

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

wchar_t const kTestClassName[] = L"AppTraverseTestLoadingWindow6C1";
wchar_t const kTestTitle[] = L"Test loading title";
wchar_t const kTestMessage[] = L"Test loading message";

void PumpPendingMessages() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void TestCreatesVisibleTopLevelWindow() {
  windows::WinApplicationLoadingWindow loading(kTestClassName, kTestTitle,
                                             kTestMessage, 100, 100, 280, 100);
  HWND const hwnd = loading.hwnd();
  CHECK(hwnd != nullptr);
  CHECK(IsWindow(hwnd));
  CHECK(GetParent(hwnd) == nullptr);
  CHECK(IsWindowVisible(hwnd));
}

void TestClassNameAndTitle() {
  windows::WinApplicationLoadingWindow loading(kTestClassName, kTestTitle,
                                             kTestMessage, 100, 100, 280, 100);
  HWND const hwnd = loading.hwnd();
  wchar_t class_buf[128]{};
  CHECK(GetClassNameW(hwnd, class_buf, static_cast<int>(std::size(class_buf))) >
        0);
  CHECK(class_buf == std::wstring{kTestClassName});

  wchar_t title_buf[128]{};
  CHECK(GetWindowTextW(hwnd, title_buf,
                       static_cast<int>(std::size(title_buf))) > 0);
  CHECK(title_buf == std::wstring{kTestTitle});
}

void TestPaintHandled() {
  windows::WinApplicationLoadingWindow loading(kTestClassName, kTestTitle,
                                             kTestMessage, 100, 100, 280, 100);
  HWND const hwnd = loading.hwnd();
  CHECK(InvalidateRect(hwnd, nullptr, TRUE));
  CHECK(UpdateWindow(hwnd));
  PumpPendingMessages();
}

void TestWmCloseDoesNotDestroy() {
  windows::WinApplicationLoadingWindow loading(kTestClassName, kTestTitle,
                                             kTestMessage, 100, 100, 280, 100);
  HWND const hwnd = loading.hwnd();
  SendMessageW(hwnd, WM_CLOSE, 0, 0);
  PumpPendingMessages();
  CHECK(loading.is_open());
  CHECK(IsWindow(hwnd));
}

void TestCloseAndRepeatClose() {
  windows::WinApplicationLoadingWindow loading(kTestClassName, kTestTitle,
                                             kTestMessage, 100, 100, 280, 100);
  HWND const hwnd = loading.hwnd();
  loading.Close();
  CHECK(!loading.is_open());
  CHECK(!IsWindow(hwnd));
  loading.Close();
}

void TestDestructorClosesWindow() {
  HWND observed{nullptr};
  {
    windows::WinApplicationLoadingWindow loading(
        kTestClassName, kTestTitle, kTestMessage, 100, 100, 280, 100);
    observed = loading.hwnd();
    CHECK(IsWindow(observed));
  }
  CHECK(!IsWindow(observed));
}

void TestDestructorUnregistersClass() {
  {
    windows::WinApplicationLoadingWindow loading(
        kTestClassName, kTestTitle, kTestMessage, 100, 100, 280, 100);
    CHECK(loading.is_open());
  }
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = instance;
  wc.lpszClassName = kTestClassName;
  CHECK(RegisterClassW(&wc) != 0);
  CHECK(UnregisterClassW(kTestClassName, instance) != 0);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestCreatesVisibleTopLevelWindow();
  apptraverse::test::TestClassNameAndTitle();
  apptraverse::test::TestPaintHandled();
  apptraverse::test::TestWmCloseDoesNotDestroy();
  apptraverse::test::TestCloseAndRepeatClose();
  apptraverse::test::TestDestructorClosesWindow();
  apptraverse::test::TestDestructorUnregistersClass();
  std::cout << "win_application_loading_window_test OK\n";
  return 0;
}
