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

void TestLocalizedTextPaintsImmediately() {
  windows::WinApplicationLoadingWindow loading(kTestClassName, L"AppTraverse",
                                             L"", 100, 100, 320, 120);
  // Keep this independent of the source file's encoding.
  std::wstring const text = L"\u0417\u0430\u0433\u0440\u0443\u0437\u043a\u0430 \u043a\u0430\u0442\u0430\u043b\u043e\u0433\u0430";
  loading.SetText(text, text);
  wchar_t title[128]{};
  CHECK(GetWindowTextW(loading.hwnd(), title,
                       static_cast<int>(std::size(title))) > 0);
  CHECK(std::wstring{title} == text);
  CHECK(GetUpdateRect(loading.hwnd(), nullptr, FALSE) == 0);
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
  apptraverse::test::TestLocalizedTextPaintsImmediately();
  apptraverse::test::TestWmCloseDoesNotDestroy();
  apptraverse::test::TestCloseAndRepeatClose();
  apptraverse::test::TestDestructorClosesWindow();
  apptraverse::test::TestDestructorUnregistersClass();
  std::cout << "win_application_loading_window_test OK\n";
  return 0;
}
