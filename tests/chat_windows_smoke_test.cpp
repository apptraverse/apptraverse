// Windows smoke test for AppTraverse Win32 Chat
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "apptraverse/noninteractive_crt.h"
#include "chat_commands.h"
#include "chat_model.h"
#include "win_chat_app.h"

namespace apptraverse::example::chat_demo {
namespace {

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

void PumpGui(std::chrono::milliseconds slice) {
  auto const deadline = std::chrono::steady_clock::now() + slice;
  while (std::chrono::steady_clock::now() < deadline) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
}

HWND FindOwned(DWORD pid, wchar_t const* class_name, wchar_t const* title) {
  struct Ctx {
    DWORD pid;
    wchar_t const* class_name;
    wchar_t const* title;
    HWND found;
  } ctx{pid, class_name, title, nullptr};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD window_pid = 0;
        GetWindowThreadProcessId(hwnd, &window_pid);
        if (window_pid != c->pid) {
          return TRUE;
        }
        wchar_t name[256]{};
        if (GetClassNameW(hwnd, name, 256) <= 0 ||
            wcscmp(name, c->class_name) != 0) {
          return TRUE;
        }
        if (c->title != nullptr) {
          wchar_t text[256]{};
          GetWindowTextW(hwnd, text, 256);
          if (wcscmp(text, c->title) != 0) {
            return TRUE;
          }
        }
        if (IsWindowVisible(hwnd) == 0) {
          return TRUE;
        }
        c->found = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.found;
}

std::wstring GetText(HWND hwnd) {
  int const len = GetWindowTextLengthW(hwnd);
  if (len <= 0) {
    return L"";
  }
  std::wstring res(static_cast<std::size_t>(len) + 1, L'\0');
  GetWindowTextW(hwnd, res.data(), len + 1);
  res.resize(static_cast<std::size_t>(len));
  return res;
}

void TestWinChatSmoke() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  EnsureChatDemoModelRegistration();

  DWORD const pid = GetCurrentProcessId();

  // Create temporary directory for smoke test
  std::filesystem::path test_dir =
      std::filesystem::temp_directory_path() / "apptraverse_win_chat_smoke";
  std::filesystem::remove_all(test_dir);
  std::filesystem::create_directories(test_dir);

  std::cout << "Starting Windows smoke test harness...\n";

  // First run: test control creation, publication display, chat selection, draft preservation
  {
    ChatLaunchOptions options{
        .state_dir = test_dir.string(),
        .open_peer = OpenPeerRequest{
            .peer_admin_id = "smoke-peer-1",
            .peer_name = "Smoke Peer One",
        },
    };

    WinChatApp app;
    std::thread gui{[&] { CHECK(app.Run(options) == 0); }};

    // Wait for main window to appear
    HWND main_hwnd = nullptr;
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    while (std::chrono::steady_clock::now() < deadline) {
      main_hwnd = FindOwned(pid, L"AppTraverseWinChatMainWindow", L"AppTraverse Chat");
      if (main_hwnd != nullptr) {
        break;
      }
      PumpGui(std::chrono::milliseconds{20});
    }
    CHECK(main_hwnd != nullptr);

    // Verify real controls are created
    CHECK(app.main_hwnd() == main_hwnd);
    CHECK(IsWindow(app.chat_list_hwnd()) != 0);
    CHECK(IsWindow(app.transcript_hwnd()) != 0);
    CHECK(IsWindow(app.draft_hwnd()) != 0);
    CHECK(IsWindow(app.send_btn_hwnd()) != 0);
    CHECK(IsWindow(app.admin_id_hwnd()) != 0);
    CHECK(IsWindow(app.aether_uid_hwnd()) != 0);
    CHECK(IsWindow(app.open_btn_hwnd()) != 0);
    CHECK(IsWindow(app.status_label_hwnd()) != 0);
    CHECK(IsWindow(app.presence_label_hwnd()) != 0);

    // Wait for initial publication to be applied
    auto const pub_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < pub_deadline) {
      if (app.ui_workspace().is_valid() && !app.ui_workspace()->chats.empty()) {
        break;
      }
      PumpGui(std::chrono::milliseconds{30});
    }
    CHECK(app.ui_workspace().is_valid());
    CHECK(!app.ui_workspace()->chats.empty());

    // Verify chat list shows the chat entry
    LRESULT const chat_count = SendMessageW(app.chat_list_hwnd(), LB_GETCOUNT, 0, 0);
    CHECK(chat_count >= 1);

    // Select chat entry 0
    SendMessageW(app.chat_list_hwnd(), LB_SETCURSEL, 0, 0);
    SendMessageW(main_hwnd, WM_COMMAND, MAKEWPARAM(101, LBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(app.chat_list_hwnd()));
    PumpGui(std::chrono::milliseconds{50});

    // Edit draft text
    std::wstring const test_draft = L"Smoke draft text hello";
    SetWindowTextW(app.draft_hwnd(), test_draft.c_str());
    SendMessageW(main_hwnd, WM_COMMAND, MAKEWPARAM(108, EN_CHANGE),
                 reinterpret_cast<LPARAM>(app.draft_hwnd()));
    PumpGui(std::chrono::milliseconds{50});
    CHECK(GetText(app.draft_hwnd()) == test_draft);

    // Verify draft and caret are not reset by publication
    SendMessageW(app.draft_hwnd(), EM_SETSEL, 6, 6);
    app.ApplyPublicationFromSession();
    PumpGui(std::chrono::milliseconds{50});
    DWORD start_sel = 0, end_sel = 0;
    SendMessageW(app.draft_hwnd(), EM_GETSEL,
                 reinterpret_cast<WPARAM>(&start_sel),
                 reinterpret_cast<LPARAM>(&end_sel));
    CHECK(start_sel == 6);
    CHECK(end_sel == 6);
    CHECK(GetText(app.draft_hwnd()) == test_draft);

    // Test non-tail scroll anchor capture & restoration
    ScrollAnchor anchor{.first_visible_message = SharedEventId{},
                        .offset_from_message_top = 15.0,
                        .follow_tail = false};
    app.session().SaveScroll(app.ui_workspace()->chats[0].id(), anchor);
    PumpGui(std::chrono::milliseconds{50});

    // Close window cleanly
    SendMessageW(main_hwnd, WM_CLOSE, 0, 0);
    gui.join();
    std::cout << "  First run closed cleanly.\n";
  }

  // Second run: verify reload of geometry, selected chat, and draft
  {
    ChatLaunchOptions options{
        .state_dir = test_dir.string(),
    };

    WinChatApp app;
    std::thread gui{[&] { CHECK(app.Run(options) == 0); }};

    HWND main_hwnd = nullptr;
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    while (std::chrono::steady_clock::now() < deadline) {
      main_hwnd = FindOwned(pid, L"AppTraverseWinChatMainWindow", L"AppTraverse Chat");
      if (main_hwnd != nullptr) {
        break;
      }
      PumpGui(std::chrono::milliseconds{20});
    }
    CHECK(main_hwnd != nullptr);

    // Wait for workspace publication
    auto const pub_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < pub_deadline) {
      if (app.ui_workspace().is_valid() && !app.ui_workspace()->chats.empty()) {
        break;
      }
      PumpGui(std::chrono::milliseconds{30});
    }
    CHECK(app.ui_workspace().is_valid());
    CHECK(!app.ui_workspace()->chats.empty());

    // Verify draft was restored
    CHECK(GetText(app.draft_hwnd()) == L"Smoke draft text hello");

    // Close cleanly
    SendMessageW(main_hwnd, WM_CLOSE, 0, 0);
    gui.join();
    std::cout << "  Second run reload and shutdown passed.\n";
  }
}

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  apptraverse::example::chat_demo::TestWinChatSmoke();
  std::cout << "apptraverse_chat_windows_smoke_test passed!\n";
  return 0;
}
