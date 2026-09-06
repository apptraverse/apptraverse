// Win32 control-state checks for the chat identity bar. HWND-only; no OCR.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/distill.h"

#include "chat_bootstrap.h"
#include "chat_commands.h"
#include "chat_identity_bar.h"
#include "chat_model.h"
#include "win_presenters.h"

namespace {

using chat::ChatCreateOptions;
using chat::ChatRole;
using chat::kIdentityBarNoInterface;
using chat::kIdentityBarNoInternet;
using chat::kIdentityBarRegistering;
using chat::win32::WinChatPresentationApplication;
using chat::win32::WinIdentityBarPresenter;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void Pump() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

std::wstring WindowText(HWND hwnd) {
  if (hwnd == nullptr) {
    return {};
  }
  int const n = GetWindowTextLengthW(hwnd);
  std::wstring wide(static_cast<std::size_t>(n) + 1, L'\0');
  if (n > 0) {
    GetWindowTextW(hwnd, wide.data(), n + 1);
  }
  wide.resize(static_cast<std::size_t>(n));
  return wide;
}

bool ChildHasExactText(HWND parent, wchar_t const* needle) {
  struct Ctx {
    wchar_t const* needle;
    bool found;
  } ctx{needle, false};
  EnumChildWindows(
      parent,
      [](HWND child, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t buf[256]{};
        GetWindowTextW(child, buf, 256);
        if (std::wstring{buf} == c->needle) {
          c->found = true;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.found;
}

bool EditIsReadonly(HWND edit) {
  LONG const style = GetWindowLongW(edit, GWL_STYLE);
  return (style & ES_READONLY) != 0;
}

struct Harness {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  chat::ChatApplication::ptr app;
  WinChatPresentationApplication::ptr presentation;
};

Harness MakeHarness(ChatRole role, std::string name) {
  chat::EnsureChatRegistration();
  chat::win32::EnsureChatPresenterRegistration();
  Harness h;
  ChatCreateOptions options;
  options.role = role;
  options.display_name = std::move(name);
  h.app = chat::BuildChatGraph(h.domain, options);
  apptraverse::FinalizeDistilledGraph(*h.app);
  chat::BeginCurrentRun(*h.app);
  h.presentation = chat::win32::BuildPresentationGraph(h.domain, *h.app);
  h.presentation->OnLoad();
  Pump();
  ShowWindow(h.presentation->chat_window->hwnd, SW_HIDE);
  Pump();
  return h;
}

void DestroyHarness(Harness& h) {
  if (h.presentation) {
    h.presentation->Destroy();
    Pump();
  }
}

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
  HWND host = CreateWindowExW(0, wc.lpszClassName, L"App Traverse Chat — Host",
                              WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr,
                              nullptr, inst, nullptr);
  HWND client =
      CreateWindowExW(0, wc.lpszClassName, L"App Traverse Chat — Client",
                      WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                      inst, nullptr);
  CHECK(host != nullptr);
  CHECK(client != nullptr);
  wchar_t host_title[128]{};
  wchar_t client_title[128]{};
  GetWindowTextW(host, host_title, 128);
  GetWindowTextW(client, client_title, 128);
  CHECK(std::wstring{host_title} == L"App Traverse Chat — Host");
  CHECK(std::wstring{client_title} == L"App Traverse Chat — Client");
  DestroyWindow(host);
  DestroyWindow(client);
}

void TestConnectUidValidationHelpers() {
  using chat::LooksLikeAetherUid;
  CHECK(LooksLikeAetherUid("3ac93165-3d37-4970-87a6-fa4ee27744e4"));
  CHECK(!LooksLikeAetherUid("bad"));
}

void TestHostIdentityBarRegisteringAndReady() {
  auto h = MakeHarness(ChatRole::Host, "Host");
  HWND hwnd = h.presentation->chat_window->hwnd;
  CHECK(hwnd != nullptr);
  HWND copy = GetDlgItem(hwnd, WinIdentityBarPresenter::kIdCopy);
  HWND join = GetDlgItem(hwnd, WinIdentityBarPresenter::kIdJoin);
  HWND uid = GetDlgItem(hwnd, WinIdentityBarPresenter::kIdUid);
  CHECK(copy != nullptr);
  CHECK(join == nullptr);
  CHECK(uid != nullptr);
  CHECK(IsWindowEnabled(copy) == FALSE);
  CHECK(EditIsReadonly(uid));
  CHECK(WindowText(uid) ==
        chat::win32::Utf8ToWide(std::string{kIdentityBarRegistering}));
  CHECK(!ChildHasExactText(hwnd, L"Registered"));
  CHECK(!ChildHasExactText(hwnd, L"Not connected"));
  CHECK(h.app->room->clients.empty());

  apptraverse::CommitNetworkInterfaceUnavailable(*h.app->network,
                                                 h.app->runtime->run_id);
  h.presentation->PresentChatWindow();
  Pump();
  CHECK(WindowText(uid) ==
        chat::win32::Utf8ToWide(std::string{kIdentityBarNoInterface}));
  CHECK(IsWindowEnabled(copy) == FALSE);

  apptraverse::CommitInternetUnavailable(*h.app->network,
                                         h.app->runtime->run_id);
  h.presentation->PresentChatWindow();
  Pump();
  CHECK(WindowText(uid) ==
        chat::win32::Utf8ToWide(std::string{kIdentityBarNoInternet}));
  CHECK(IsWindowEnabled(copy) == FALSE);

  std::string const local_uid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
  chat::CompleteLocalRegistration(*h.app, local_uid);
  h.presentation->PresentChatWindow();
  Pump();
  CHECK(WindowText(uid) == chat::win32::Utf8ToWide(local_uid));
  CHECK(IsWindowEnabled(copy) == TRUE);
  CHECK(EditIsReadonly(uid));
  CHECK(!ChildHasExactText(hwnd, L"Registered"));
  CHECK(h.app->room->journal.size() == 1);
  DestroyHarness(h);
}

void TestClientIdentityBarRegisteringAndReady() {
  auto h = MakeHarness(ChatRole::Client, "Client");
  HWND hwnd = h.presentation->chat_window->hwnd;
  CHECK(hwnd != nullptr);
  HWND copy = GetDlgItem(hwnd, WinIdentityBarPresenter::kIdCopy);
  HWND join = GetDlgItem(hwnd, WinIdentityBarPresenter::kIdJoin);
  HWND uid = GetDlgItem(hwnd, WinIdentityBarPresenter::kIdUid);
  CHECK(copy == nullptr);
  CHECK(join != nullptr);
  CHECK(uid != nullptr);
  CHECK(IsWindowEnabled(join) == FALSE);
  CHECK(EditIsReadonly(uid));
  CHECK(WindowText(uid) ==
        chat::win32::Utf8ToWide(std::string{kIdentityBarRegistering}));
  CHECK(!ChildHasExactText(hwnd, L"Registered"));

  std::string const local_uid = "bbbbbbbb-cccc-dddd-eeee-ffffffffffff";
  chat::CompleteLocalRegistration(*h.app, local_uid);
  h.presentation->PresentChatWindow();
  Pump();
  CHECK(IsWindowEnabled(join) == TRUE);
  CHECK(!EditIsReadonly(uid));
  auto const field = WindowText(uid);
  CHECK(field.find(chat::win32::Utf8ToWide(local_uid)) == std::wstring::npos);
  CHECK(!ChildHasExactText(hwnd, L"Registered"));
  DestroyHarness(h);
}

}  // namespace

int main() {
  try {
    TestContactsListBoxOwnerDrawStyles();
    TestBoldFontFromControlFont();
    TestWindowTitlesHostClient();
    TestConnectUidValidationHelpers();
    TestHostIdentityBarRegisteringAndReady();
    TestClientIdentityBarRegisteringAndReady();
    std::cout << "chat_win32_ui_smoke_test OK\n";
    return 0;
  } catch (...) {
    std::cerr << "unknown exception\n";
    return 3;
  }
}
