// Narrow Win32 UI smoke checks for chat contacts owner-draw + fonts/titles.
// No screenshot/OCR; HWND-only style and font lifetime assertions.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

#include "chat_connection_ui_state.h"

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void TestContactsListBoxOwnerDrawStyles() {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  HWND list = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", L"",
      WS_POPUP | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0,
      0, 200, 200, nullptr, nullptr, inst, nullptr);
  CHECK(list != nullptr);
  LONG style = GetWindowLongW(list, GWL_STYLE);
  CHECK((style & LBS_OWNERDRAWFIXED) != 0);
  CHECK((style & LBS_HASSTRINGS) != 0);
  CHECK((style & LBS_NOINTEGRALHEIGHT) != 0);
  DestroyWindow(list);
}

void TestBoldFontFromControlFont() {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  HWND list = CreateWindowExW(0, L"LISTBOX", L"", WS_POPUP, 0, 0, 100, 100,
                              nullptr, nullptr, inst, nullptr);
  CHECK(list != nullptr);
  HFONT control =
      reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
  LOGFONTW lf{};
  if (control != nullptr) {
    CHECK(GetObjectW(control, sizeof(lf), &lf) != 0);
  } else {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                          0);
    lf = metrics.lfMessageFont;
  }
  HFONT normal = CreateFontIndirectW(&lf);
  lf.lfWeight = FW_BOLD;
  HFONT bold = CreateFontIndirectW(&lf);
  CHECK(normal != nullptr);
  CHECK(bold != nullptr);
  LOGFONTW bold_lf{};
  CHECK(GetObjectW(bold, sizeof(bold_lf), &bold_lf) != 0);
  CHECK(bold_lf.lfWeight >= FW_BOLD);
  DeleteObject(normal);
  DeleteObject(bold);
  DestroyWindow(list);
}

void TestWindowTitlesHostClient() {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = inst;
  wc.lpszClassName = L"AppTraverseChatWin32Smoke";
  RegisterClassW(&wc);
  HWND host = CreateWindowExW(0, wc.lpszClassName, L"AppTraverse Chat — Host",
                              WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr,
                              nullptr, inst, nullptr);
  HWND client =
      CreateWindowExW(0, wc.lpszClassName, L"AppTraverse Chat — Client",
                      WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                      inst, nullptr);
  CHECK(host != nullptr);
  CHECK(client != nullptr);
  wchar_t host_title[128]{};
  wchar_t client_title[128]{};
  GetWindowTextW(host, host_title, 128);
  GetWindowTextW(client, client_title, 128);
  CHECK(std::wstring{host_title} == L"AppTraverse Chat — Host");
  CHECK(std::wstring{client_title} == L"AppTraverse Chat — Client");
  DestroyWindow(host);
  DestroyWindow(client);
}

void TestConnectUidValidationHelpers() {
  using apptraverse::LooksLikeAetherUid;
  CHECK(LooksLikeAetherUid("3ac93165-3d37-4970-87a6-fa4ee27744e4"));
  CHECK(!LooksLikeAetherUid("bad"));
}

}  // namespace

int main() {
  try {
    TestContactsListBoxOwnerDrawStyles();
    TestBoldFontFromControlFont();
    TestWindowTitlesHostClient();
    TestConnectUidValidationHelpers();
    std::cout << "chat_win32_ui_smoke_test OK\n";
    return 0;
  } catch (...) {
    std::cerr << "unknown exception\n";
    return 3;
  }
}
