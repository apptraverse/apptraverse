#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "aether/all.h"

#include "apptraverse/directory_domain_storage.h"

#include "model/application_ids.h"
#include "model/app.h"
#include "model/chat.h"
#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"
#include "model/registration.h"
#include "model/window_changed_event.h"

#include "../common/aether_p2p_transport.h"
#include "../common/aether_runtime.h"
#include "../common/chat_sync_controller.h"
#include "../common/chat_transcript.h"
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

struct CliOptions {
  bool distill{false};
  std::filesystem::path state_dir{"state"};
  std::string aether_client_name{
      apptraverse::examples::kWindowsAetherClientName};
  bool print_aether_uid{false};
  std::optional<ae::Uid> peer;
  bool auto_accept_peer{false};
  std::optional<std::string> send_after_sync;
  std::optional<std::string> wait_for_message;
  bool exit_after_message{false};
  std::optional<ae::Uid> p2p_ping;
  bool parse_error{false};
};

std::optional<ae::Uid> ParseUidArg(char const* text, char const* flag) {
  auto const uid = ae::Uid::FromString(std::string_view{text});
  if (uid.empty()) {
    std::cerr << "Invalid " << flag << " UID\n";
    return ae::Uid{};
  }
  return uid;
}

CliOptions ParseCli(int argc, char** argv) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    auto need_value = [&](char const* flag) -> char const* {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << flag << '\n';
        options.parse_error = true;
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--distill") {
      options.distill = true;
    } else if (arg == "--state-dir") {
      if (auto const* value = need_value("--state-dir")) {
        options.state_dir = value;
      }
    } else if (arg == "--aether-client-name") {
      if (auto const* value = need_value("--aether-client-name")) {
        options.aether_client_name = value;
      }
    } else if (arg == "--print-aether-uid") {
      options.print_aether_uid = true;
    } else if (arg == "--peer") {
      if (auto const* value = need_value("--peer")) {
        options.peer = ParseUidArg(value, "--peer");
        if (options.peer.has_value() && options.peer->empty()) {
          options.parse_error = true;
        }
      }
    } else if (arg == "--auto-accept-peer") {
      options.auto_accept_peer = true;
    } else if (arg == "--send-after-sync") {
      if (auto const* value = need_value("--send-after-sync")) {
        options.send_after_sync = value;
      }
    } else if (arg == "--wait-for-message") {
      if (auto const* value = need_value("--wait-for-message")) {
        options.wait_for_message = value;
      }
    } else if (arg == "--exit-after-message") {
      options.exit_after_message = true;
    } else if (arg == "--p2p-ping") {
      if (auto const* value = need_value("--p2p-ping")) {
        options.p2p_ping = ParseUidArg(value, "--p2p-ping");
        if (options.p2p_ping.has_value() && options.p2p_ping->empty()) {
          options.parse_error = true;
        }
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      options.parse_error = true;
    }
  }
  return options;
}

apptraverse::examples::ConstructedAetherRuntime ConstructWindowsAetherApp(
    std::filesystem::path const& state_dir) {
  auto root = state_dir;
  return apptraverse::examples::ConstructAetherAppWithEthernet(
      [root]() {
        return std::make_unique<apptraverse::DirectoryDomainStorage>(root);
      });
}

void Distill(std::filesystem::path const& state_dir) {
  std::filesystem::remove_all(state_dir);

  auto runtime = ConstructWindowsAetherApp(state_dir);
  auto graph =
      apptraverse::examples::BuildSingleClientChatGraph<
          apptraverse::WindowsWindow, apptraverse::WinWindowPresenter,
          apptraverse::WinChatPresenter>(runtime.app->domain(), "Windows");

  graph.app.Save();
  std::cout << "Distilled single-client chat graph to " << state_dir.string()
            << '\n';
  std::cout << "APP_CLIENT_READY platform=windows obj_id="
            << graph.local_client.id().id() << " name="
            << graph.local_client->name << '\n';
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

int Run(CliOptions const& options) {
  if (options.p2p_ping.has_value() && options.p2p_ping->empty()) {
    return 1;
  }
  if (options.peer.has_value() && options.peer->empty()) {
    return 1;
  }

  auto runtime = ConstructWindowsAetherApp(options.state_dir);
  auto aether_app = runtime.app;
  auto* domain_storage = runtime.storage;
  if (aether_app.get() == nullptr || domain_storage == nullptr) {
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

  auto local_client = app->local_client;
  local_client.Load();
  if (!local_client.is_loaded()) {
    std::cerr << "Failed to load App.local_client\n";
    return 1;
  }
  LogLine("APP_CLIENT_READY platform=windows obj_id=" +
          std::to_string(local_client.id().id()) +
          " name=" + local_client->name);

  auto aether_client = apptraverse::examples::SelectPersistentAetherClient(
      aether_app, options.aether_client_name);
  if (!aether_client) {
    std::cerr << "Failed to select Aether client\n";
    return 1;
  }
  LogLine("AETHER_CLIENT_READY platform=windows uid=" +
          apptraverse::examples::FormatAetherUid(aether_client->uid()));
  if (options.print_aether_uid) {
    LogLine("AETHER_UID=" +
            apptraverse::examples::FormatAetherUid(aether_client->uid()));
    return 0;
  }

  apptraverse::examples::AetherP2pTransport p2p_transport;
  p2p_transport.Start(aether_app, aether_client);

  auto& win_presenter =
      static_cast<apptraverse::WinWindowPresenter&>(*presenter);

  win_presenter.chat_presenter.Load();
  if (!win_presenter.chat_presenter.is_loaded() ||
      win_presenter.chat_presenter->GetClassId() !=
          apptraverse::WinChatPresenter::kClassId) {
    std::cerr << "Expected WinChatPresenter after load\n";
    return 1;
  }
  auto& chat_ui =
      static_cast<apptraverse::WinChatPresenter&>(*win_presenter.chat_presenter);
  chat_ui.chat.Load();
  if (!chat_ui.chat.is_loaded()) {
    std::cerr << "Failed to load Chat\n";
    return 1;
  }
  auto chat = chat_ui.chat;
  auto peer_set = chat->peer_set;
  peer_set.Load();
  if (!peer_set.is_loaded()) {
    std::cerr << "Failed to load ChatPeerSet\n";
    return 1;
  }

  auto log_chat_journal = [&]() {
    chat.Load();
    if (!chat.is_loaded()) {
      return;
    }
    LogLine("CHAT_JOURNAL_SIZE n=" + std::to_string(chat->journal.size()));
  };

  bool sent_after_sync = false;
  bool saw_wait_message = false;
  bool logged_wait_message = false;
  std::function<void()> on_pong_received;

  auto transcript_contains = [&](std::string const& needle) {
    chat.Load();
    if (!chat.is_loaded()) {
      return false;
    }
    auto const transcript =
        apptraverse::examples::FormatChatTranscriptUtf8(chat);
    return transcript.find(needle) != std::string::npos;
  };

  auto check_wait_message = [&]() {
    if (!options.wait_for_message.has_value() || saw_wait_message) {
      return;
    }
    if (!transcript_contains(*options.wait_for_message)) {
      return;
    }
    saw_wait_message = true;
    if (!logged_wait_message) {
      LogLine("CHAT_MESSAGE_VISIBLE text=" + *options.wait_for_message);
      logged_wait_message = true;
    }
    if (options.exit_after_message) {
      aether_app->Exit(0);
    }
  };

  apptraverse::examples::ChatSyncController chat_sync(
      apptraverse::SyncReplica{aether_app->domain(), *domain_storage,
                               chat.id()},
      chat, peer_set,
      [&](ae::Uid const& peer, apptraverse::SerializedSyncPacket const& bytes) {
        p2p_transport.Send(peer, bytes);
      },
      options.auto_accept_peer,
      [&]() {
        chat_ui.RefreshTranscript();
        check_wait_message();
        app.Save();
      },
      LogLine);
  chat_sync.Start();

  p2p_transport.SetReceiveHandler(
      [&](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
        if (apptraverse::examples::TryHandleP2pProbePayload(
                p2p_transport, peer, payload, LogLine, on_pong_received)) {
          return;
        }
        chat_sync.Receive(peer, payload);
      });

  if (options.peer.has_value()) {
    chat_sync.AddPeer(*options.peer);
    p2p_transport.Connect(*options.peer);
  }

  win_presenter.CreateNativeWindow();
  log_chat_journal();
  check_wait_message();

  ae::TimePoint ping_deadline{};
  ae::TimePoint const* deadline_ptr = nullptr;
  if (options.p2p_ping.has_value()) {
    on_pong_received = [aether_app, log_chat_journal]() {
      log_chat_journal();
      aether_app->Exit(0);
    };
    apptraverse::examples::SendP2pPing(p2p_transport, *options.p2p_ping,
                                       LogLine);
    ping_deadline = ae::Now() + kP2pPingTimeout;
    deadline_ptr = &ping_deadline;
  }

  auto maybe_send_after_sync = [&]() {
    if (!options.send_after_sync.has_value() || sent_after_sync) {
      return;
    }
    apptraverse::SharedGraphSyncSession* session = nullptr;
    if (options.peer.has_value()) {
      session = chat_sync.FindSession(*options.peer);
    } else if (chat_sync.runtime_session_count() > 0) {
      peer_set.Load();
      if (peer_set.is_loaded() && !peer_set->peers.empty()) {
        session = chat_sync.FindSession(peer_set->peers.front().remote_uid);
      }
    }
    if (session == nullptr || !session->initial_sync_complete()) {
      return;
    }
    chat_ui.SubmitText(*options.send_after_sync);
    chat.Save();
    app.Save();
    chat_ui.RefreshTranscript();
    sent_after_sync = true;
    LogLine("CHAT_SEND_AFTER_SYNC text=" + *options.send_after_sync);
  };

  for (;;) {
    int const quit_code = ProcessPendingWin32Messages();
    if (quit_code >= 0) {
      return quit_code;
    }
    if (aether_app->IsExited()) {
      return aether_app->ExitCode();
    }
    if (deadline_ptr != nullptr && ae::Now() >= *deadline_ptr) {
      std::cerr << "P2P ping timed out waiting for PONG\n";
      return 1;
    }
    if (options.wait_for_message.has_value() && saw_wait_message &&
        options.exit_after_message) {
      return 0;
    }

    auto const now = ae::Now();
    auto const next_update = aether_app->Update(now);
    chat_sync.Tick(now);
    maybe_send_after_sync();
    check_wait_message();

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

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  auto options = ParseCli(argc, argv);
  if (options.parse_error) {
    return 1;
  }
  if (options.distill) {
    Distill(options.state_dir);
    return 0;
  }
  return Run(options);
}
