// Windows smoke test for AppTraverse Win32 Chat
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "aether_link.h"
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

inline constexpr ae::ObjId kWorkspaceRootId{10001};

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
        if (GetClassNameW(hwnd, name, 256) <= 0 || wcscmp(name, c->class_name) != 0) {
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

void SaveWorkspaceGraph(ChatWorkspace::ptr const& ws) {
  ws.Save();
  for (auto& entry : ws->chats) {
    if (entry.is_valid()) {
      entry.Save();
      if (entry->peer_link.is_valid()) {
        entry->peer_link.Save();
      }
      if (entry->room.is_valid()) {
        entry->room.Save();
      }
    }
  }
}

AetherLink::ptr CreateAetherLink(ae::Domain& domain, ae::ObjId id, std::string endpoint) {
  auto link = AetherLink::ptr::Create(ae::CreateWith{domain}.with_id(id));
  link->endpoint_uid = std::move(endpoint);
  InitializeRuntimeNode(*link);
  return link;
}

ChatRoom::ptr CreateRoom(ae::Domain& domain, ae::ObjId id) {
  auto room = ChatRoom::ptr::Create(ae::CreateWith{domain}.with_id(id));
  InitializeRuntimeNode(*room);
  return room;
}

void SeedSmokeWorkspace(std::filesystem::path const& state_dir) {
  auto const model_dir = state_dir / "model";
  std::filesystem::remove_all(model_dir);
  std::filesystem::create_directories(model_dir);

  EnsureAetherLinkRegistration();
  apptraverse::DirectoryDomainStorage storage{model_dir};
  ae::Domain domain{storage};

  auto ws = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(kWorkspaceRootId));
  InitializeRuntimeNode(*ws);
  ConfigureDemoRole(*ws, DemoRole::kHost);
  BindLocalEndpoint(*ws, "11111111-2222-3333-4444-555555555555");

  DesktopBounds bounds{
      .valid = true,
      .x = -120,
      .y = 80,
      .width = 920,
      .height = 640,
      .maximized = false,
  };
  SetDesktopBounds(*ws, bounds);

  auto e1 = OpenOrSelectChat(*ws, "22222222-3333-4444-5555-666666666666", [] {});
  auto e2 = OpenOrSelectChat(*ws, "33333333-4444-5555-6666-777777777777", [] {});
  SelectChat(*ws, e1.id());

  auto link1 = CreateAetherLink(domain, ae::ObjId{2001}, "22222222-3333-4444-5555-666666666666");
  auto room1 = CreateRoom(domain, ae::ObjId{3001});
  BindChat(*e1, link1, room1);

  auto link2 = CreateAetherLink(domain, ae::ObjId{2002}, "33333333-4444-5555-6666-777777777777");
  auto room2 = CreateRoom(domain, ae::ObjId{3002});
  BindChat(*e2, link2, room2);

  for (int i = 0; i < 180; ++i) {
    std::string text = "Message " + std::to_string(i) +
                       "\nEmoji line\nWrapped multiline body with enough height to "
                       "scroll far past sixty-five thousand logical pixels when accumulated.";
    SetDraft(*e1, text);
    SubmitDraft(*ws, *e1, static_cast<std::uint64_t>(10000 + i));
  }

  SetDraft(*e1, "Alpha draft preserved");
  SetDraft(*e2, "Beta draft preserved");

  ScrollAnchor anchor_mid{
      .follow_tail = false,
      .first_visible_message = room1->messages[40].id,
      .offset_from_message_top = 18.0,
  };
  SetScroll(*e1, anchor_mid);

  SaveWorkspaceGraph(ws);
}

bool WaitForSnapshot(WinChatApp& app, WinChatGuiSnapshot& snap,
                     std::chrono::milliseconds timeout, int min_chats = 1) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (app.TryQueryGuiSnapshot(snap) && snap.workspace_ready &&
        snap.chat_count >= min_chats) {
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return app.TryQueryGuiSnapshot(snap) && snap.workspace_ready &&
         snap.chat_count >= min_chats;
}

HWND WaitForMainWindow(WinChatApp& app) {
  HWND main_hwnd = nullptr;
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
  while (std::chrono::steady_clock::now() < deadline) {
    main_hwnd = app.main_hwnd();
    if (main_hwnd != nullptr && IsWindow(main_hwnd) != 0) {
      return main_hwnd;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return nullptr;
}

void TestWinChatSmoke() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  EnsureChatDemoModelRegistration();
  EnsureAetherLinkRegistration();

  DWORD const pid = GetCurrentProcessId();
  std::filesystem::path const test_dir =
      std::filesystem::temp_directory_path() / "apptraverse_win_chat_smoke";
  std::filesystem::remove_all(test_dir);
  std::filesystem::create_directories(test_dir);

  SeedSmokeWorkspace(test_dir);

  std::cout << "Starting Windows smoke test harness...\n";

  {
    ChatLaunchOptions options{.role = DemoRole::kHost, .state_dir = test_dir.string()};

    WinChatApp app;
    std::thread gui{[&] { CHECK(app.Run(options) == 0); }};

    HWND const main_hwnd = WaitForMainWindow(app);
    CHECK(main_hwnd != nullptr);
    CHECK(app.main_hwnd() == main_hwnd);
    CHECK(IsWindow(app.chat_list_hwnd()) != 0);
    CHECK(IsWindow(app.transcript_hwnd()) != 0);
    CHECK(IsWindow(app.draft_hwnd()) != 0);
    CHECK(IsWindow(app.join_error_hwnd()) != 0);
    CHECK(IsWindowVisible(app.join_error_hwnd()) == FALSE);

    WinChatGuiSnapshot snap{};
    CHECK(WaitForSnapshot(app, snap, std::chrono::seconds{15}, 2));
    CHECK(snap.chat_count >= 2);
    CHECK(snap.draft.find(L"Alpha draft preserved") != std::wstring::npos);
    CHECK(snap.bounds.valid);
    CHECK(snap.bounds.x == -120);
    CHECK(snap.bounds.width == 920);

    LRESULT const list_count = SendMessageW(app.chat_list_hwnd(), LB_GETCOUNT, 0, 0);
    CHECK(list_count >= 2);
    SendMessageW(main_hwnd, WM_APP + 22, 1, 0);
    PumpGui(std::chrono::milliseconds{150});
    CHECK(app.TryQueryGuiSnapshot(snap));
    CHECK(snap.list_selection == 1);
    CHECK(snap.draft.find(L"Beta draft preserved") != std::wstring::npos);

    SendMessageW(main_hwnd, WM_APP + 22, 0, 0);
    PumpGui(std::chrono::milliseconds{80});
    CHECK(app.TryQueryGuiSnapshot(snap));
    CHECK(snap.draft.find(L"Alpha draft preserved") != std::wstring::npos);

    std::wstring const typed = L"Typing during incoming";
    SetWindowTextW(app.draft_hwnd(), typed.c_str());
    SendMessageW(main_hwnd, WM_COMMAND, MAKEWPARAM(108, EN_CHANGE),
                 reinterpret_cast<LPARAM>(app.draft_hwnd()));
    SendMessageW(main_hwnd, WM_APP + 21, 0, 0);
    PumpGui(std::chrono::milliseconds{80});
    CHECK(app.TryQueryGuiSnapshot(snap));
    CHECK(snap.draft == typed);

    CHECK(!snap.model_scroll.follow_tail);
    CHECK(snap.active_entry_id.is_valid());
    double const seeded_offset = snap.model_scroll.offset_from_message_top;
    CHECK(std::abs(seeded_offset - 18.0) < 0.1);

    SendMessageW(app.transcript_hwnd(), WM_VSCROLL, SB_LINEUP, 0);
    PumpGui(std::chrono::milliseconds{50});
    CHECK(app.TryQueryGuiSnapshot(snap));
    ScrollAnchor const captured = snap.measured_scroll;
    CHECK(!captured.follow_tail);
    app.session().SaveScroll(snap.active_entry_id, captured);
    PumpGui(std::chrono::milliseconds{200});

    SendMessageW(main_hwnd, WM_APP + 22, 1, 0);
    PumpGui(std::chrono::milliseconds{80});
    SendMessageW(main_hwnd, WM_APP + 22, 0, 0);
    PumpGui(std::chrono::milliseconds{150});
    CHECK(app.TryQueryGuiSnapshot(snap));
    CHECK(snap.measured_scroll.first_visible_message == captured.first_visible_message);
    CHECK(std::abs(snap.measured_scroll.offset_from_message_top -
                   captured.offset_from_message_top) < 6.0);

    LONG const text_len = GetWindowTextLengthW(app.transcript_hwnd());
    CHECK(text_len > 1000);

    ShowWindow(main_hwnd, SW_SHOWMAXIMIZED);
    PumpGui(std::chrono::milliseconds{100});
    SendMessageW(main_hwnd, WM_CLOSE, 0, 0);
    gui.join();
    std::cout << "  First run closed cleanly.\n";
  }

  {
    ChatLaunchOptions options{.role = DemoRole::kHost, .state_dir = test_dir.string()};

    WinChatApp app;
    std::thread gui{[&] { CHECK(app.Run(options) == 0); }};

    HWND const main_hwnd = WaitForMainWindow(app);
    CHECK(main_hwnd != nullptr);

    WinChatGuiSnapshot snap{};
    CHECK(WaitForSnapshot(app, snap, std::chrono::seconds{15}, 2));
    CHECK(snap.bounds.valid);
    CHECK(snap.bounds.x == -120);

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    CHECK(GetWindowPlacement(main_hwnd, &wp) != 0);
    bool const maximized =
        wp.showCmd == SW_SHOWMAXIMIZED || ((wp.flags & WPF_RESTORETOMAXIMIZED) != 0);
    CHECK(maximized);

    CHECK(!snap.model_scroll.follow_tail);
    CHECK(std::abs(snap.measured_scroll.offset_from_message_top -
                   snap.model_scroll.offset_from_message_top) < 6.0);

    SendMessageW(main_hwnd, WM_CLOSE, 0, 0);
    gui.join();
    std::cout << "  Second run reload and shutdown passed.\n";
  }

  {
    std::filesystem::path const cold_dir = test_dir / "cold";
    std::filesystem::create_directories(cold_dir);
    ChatLaunchOptions options{
        .role = DemoRole::kHost,
        .state_dir = cold_dir.string(),
    };

    WinChatApp app;
    std::thread gui{[&] { CHECK(app.Run(options) == 0); }};
    HWND const main_hwnd = WaitForMainWindow(app);
    CHECK(main_hwnd != nullptr);
    PumpGui(std::chrono::milliseconds{100});
    SendMessageW(main_hwnd, WM_CLOSE, 0, 0);
    gui.join();
    std::cout << "  Close while connecting passed.\n";
  }
}

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  apptraverse::example::chat_demo::TestWinChatSmoke();
  std::cout << "apptraverse_chat_windows_smoke_test passed!\n";
  return 0;
}
