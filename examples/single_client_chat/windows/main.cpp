#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "aether/all.h"
#include "aether/domain_storage/file_system_std_storage.h"

#include "apptraverse/application_ids.h"
#include "apptraverse/app.h"
#include "apptraverse/window_changed_event.h"

#include "../common/aether_p2p_transport.h"
#include "../common/aether_runtime.h"
#include "../common/graph_builder.h"
#include "win_chat_presenter.h"
#include "win_window_presenter.h"
#include "windows_window.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(WindowsWindow);
APPTRAVERSE_REGISTER(WinWindowPresenter);
APPTRAVERSE_REGISTER(WinChatPresenter);

}  // namespace
}  // namespace apptraverse

namespace {

constexpr auto kMaxAetherWait = std::chrono::milliseconds{20};
constexpr auto kP2pPingTimeout = std::chrono::seconds{90};

bool IsDistillMode(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--distill") == 0) {
      return true;
    }
  }
  return false;
}

std::optional<ae::Uid> ParseP2pPingUid(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--p2p-ping") == 0) {
      auto const uid = ae::Uid::FromString(std::string_view{argv[i + 1]});
      if (uid.empty()) {
        std::cerr << "Invalid --p2p-ping UID\n";
        return ae::Uid{};
      }
      return uid;
    }
  }
  return std::nullopt;
}

ae::RcPtr<ae::AetherApp> ConstructWindowsAetherApp() {
  return apptraverse::examples::ConstructAetherAppWithEthernet([]() {
    return std::make_unique<ae::FileSystemStdStorage>();
  });
}

void Distill() {
  std::filesystem::remove_all("state");

  auto aether_app = ConstructWindowsAetherApp();
  auto graph =
      apptraverse::examples::BuildSingleClientChatGraph<
          apptraverse::WindowsWindow, apptraverse::WinWindowPresenter,
          apptraverse::WinChatPresenter>(aether_app->domain());

  graph.app.Save();
  std::cout << "Distilled single-client chat graph to ./state\n";
}

int ProcessPendingWin32Messages() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      return static_cast<int>(msg.wParam);
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return -1;
}

void LogLine(std::string const& line) {
  std::cout << line << '\n';
  std::fflush(stdout);
}

int RunCombinedLoop(ae::RcPtr<ae::AetherApp> const& aether_app,
                    ae::TimePoint const* deadline) {
  for (;;) {
    int const quit_code = ProcessPendingWin32Messages();
    if (quit_code >= 0) {
      return quit_code;
    }
    if (aether_app->IsExited()) {
      return aether_app->ExitCode();
    }
    if (deadline != nullptr && ae::Now() >= *deadline) {
      std::cerr << "P2P ping timed out waiting for PONG\n";
      return 1;
    }

    auto const next_update = aether_app->Update(ae::Now());
    auto wait_until = std::min(next_update, ae::Now() + kMaxAetherWait);
    auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       wait_until - ae::Now())
                       .count();
    if (wait_ms < 0) {
      wait_ms = 0;
    }
    if (wait_ms > kMaxAetherWait.count()) {
      wait_ms = kMaxAetherWait.count();
    }
    MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(wait_ms),
                                QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
}

int Run(std::optional<ae::Uid> p2p_ping_uid) {
  if (p2p_ping_uid.has_value() && p2p_ping_uid->empty()) {
    return 1;
  }

  auto aether_app = ConstructWindowsAetherApp();
  if (aether_app.get() == nullptr) {
    std::cerr << "Failed to construct AetherApp\n";
    return 1;
  }

  auto app = apptraverse::App::ptr::Declare(ae::CreateWith{aether_app->domain()}
                                                .with_id(apptraverse::ToObjId(
                                                    apptraverse::
                                                        ApplicationObjId::
                                                            Application)));
  app.Load();
  if (!app.is_loaded()) {
    std::cerr << "Failed to load App. Run with --distill first.\n";
    return 1;
  }

  auto window = app->window;
  window.Load();
  auto presenter = window->presenter;
  presenter.Load();

  if (presenter->GetClassId() != apptraverse::WinWindowPresenter::kClassId) {
    std::cerr << "Expected WinWindowPresenter after load\n";
    return 1;
  }

  auto aether_client = apptraverse::examples::SelectPersistentAetherClient(
      aether_app, apptraverse::examples::kWindowsAetherClientName);
  if (!aether_client) {
    std::cerr << "Failed to select Aether client\n";
    return 1;
  }
  LogLine("AETHER_CLIENT_READY platform=windows uid=" +
          apptraverse::examples::FormatAetherUid(aether_client->uid()));

  apptraverse::examples::AetherP2pTransport p2p_transport;
  p2p_transport.Start(aether_app, aether_client);

  auto& win_presenter =
      static_cast<apptraverse::WinWindowPresenter&>(*presenter);

  auto log_chat_journal = [&]() {
    win_presenter.chat_presenter.Load();
    if (!win_presenter.chat_presenter.is_loaded()) {
      return;
    }
    auto chat = win_presenter.chat_presenter->chat;
    chat.Load();
    if (!chat.is_loaded()) {
      return;
    }
    LogLine("CHAT_JOURNAL_SIZE n=" + std::to_string(chat->journal.size()));
  };

  win_presenter.CreateNativeWindow();
  log_chat_journal();

  ae::TimePoint ping_deadline{};
  ae::TimePoint const* deadline_ptr = nullptr;
  if (p2p_ping_uid.has_value()) {
    apptraverse::examples::AttachPingPongProbe(
        p2p_transport, LogLine, [aether_app, log_chat_journal]() {
          log_chat_journal();
          aether_app->Exit(0);
        });
    apptraverse::examples::SendP2pPing(p2p_transport, *p2p_ping_uid, LogLine);
    ping_deadline = ae::Now() + kP2pPingTimeout;
    deadline_ptr = &ping_deadline;
  } else {
    apptraverse::examples::AttachPingPongProbe(p2p_transport, LogLine);
  }

  return RunCombinedLoop(aether_app, deadline_ptr);
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnsureObjectRegistration();
  if (IsDistillMode(argc, argv)) {
    Distill();
    return 0;
  }
  return Run(ParseP2pPingUid(argc, argv));
}
