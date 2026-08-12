#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
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
  // Commits a message right after startup, without waiting for a synchronized
  // peer. Smoke automation only: the UI Send button uses the same path.
  std::optional<std::string> commit_message;
  // Smoke automation: poll a one-line inbox file and commit each line without
  // restarting the process (keeps the outgoing P2P path stable).
  std::optional<std::filesystem::path> commit_inbox;
  // Smoke automation: poll a one-line inbox file of peer UIDs and AddPeer without
  // restarting (path is not stored in the model).
  std::optional<std::filesystem::path> peer_inbox;
  std::optional<std::string> wait_for_message;
  bool exit_after_message{false};
  bool exit_after_pending_clear{false};
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
    } else if (arg == "--commit-message") {
      if (auto const* value = need_value("--commit-message")) {
        options.commit_message = value;
      }
    } else if (arg == "--commit-inbox") {
      if (auto const* value = need_value("--commit-inbox")) {
        options.commit_inbox = value;
      }
    } else if (arg == "--peer-inbox") {
      if (auto const* value = need_value("--peer-inbox")) {
        options.peer_inbox = value;
      }
    } else if (arg == "--wait-for-message") {
      if (auto const* value = need_value("--wait-for-message")) {
        options.wait_for_message = value;
      }
    } else if (arg == "--exit-after-message") {
      options.exit_after_message = true;
    } else if (arg == "--exit-after-pending-clear") {
      options.exit_after_pending_clear = true;
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
  p2p_transport.SetLogHandler([](std::string line) { LogLine(line); });
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
  bool saw_pending_after_commit = false;
  bool pending_cleared_after_commit = false;
  std::function<void()> on_pong_received;
  std::set<std::string> visible_message_keys;

  auto system_utc_micros = []() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  };

  auto transcript_contains = [&](std::string const& needle) {
    chat.Load();
    if (!chat.is_loaded()) {
      return false;
    }
    auto const transcript =
        apptraverse::examples::FormatChatTranscriptUtf8(chat);
    return transcript.find(needle) != std::string::npos;
  };

  auto emit_visible_keys = [&]() {
    chat.Load();
    if (!chat.is_loaded()) {
      return;
    }
    auto const transcript =
        apptraverse::examples::FormatChatTranscriptUtf8(chat);
    std::size_t start = 0;
    while (start < transcript.size()) {
      auto const end = transcript.find('\n', start);
      auto line = transcript.substr(
          start, end == std::string::npos ? std::string::npos : end - start);
      start = end == std::string::npos ? transcript.size() : end + 1;
      auto const sep = line.rfind(": ");
      if (sep == std::string::npos) {
        continue;
      }
      auto const key = line.substr(sep + 2);
      if (key.empty() || key.find(' ') != std::string::npos || key.size() > 64) {
        continue;
      }
      if (!visible_message_keys.insert(key).second) {
        continue;
      }
      LogLine("CHAT_MESSAGE_VISIBLE platform=windows text_key=" + key +
              " t_us=" + std::to_string(system_utc_micros()));
    }
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
      LogLine("CHAT_MESSAGE_VISIBLE platform=windows text_key=" +
              *options.wait_for_message +
              " t_us=" + std::to_string(system_utc_micros()));
      logged_wait_message = true;
      visible_message_keys.insert(*options.wait_for_message);
    }
    if (options.exit_after_message) {
      aether_app->Exit(0);
    }
  };

  auto sync_send = [&](ae::Uid const& peer, ae::ObjId packet_id,
                       apptraverse::SerializedSyncPacket const& bytes) {
    p2p_transport.Send(peer, bytes);
    LogLine(
        "SYNC_TRANSPORT_WRITE peer=" +
        apptraverse::examples::FormatAetherUid(peer) +
        " packet=" + std::to_string(packet_id.id()) +
        " t_us=" + std::to_string(system_utc_micros()));
  };
  auto presence_send = [&](ae::Uid const& peer,
                           std::vector<std::uint8_t> const& bytes) {
    p2p_transport.Send(peer, bytes);
  };
  auto sync_reconnect = [&](ae::Uid const& peer) {
    p2p_transport.Reconnect(peer);
  };

  apptraverse::examples::ChatSyncController chat_sync(
      apptraverse::SyncReplica{aether_app->domain(), *domain_storage,
                               chat.id()},
      chat, peer_set, sync_send, presence_send, sync_reconnect,
      apptraverse::examples::ChatSyncTiming{},
      options.auto_accept_peer,
      [&]() {
        chat_ui.RefreshTranscript();
        emit_visible_keys();
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
  } else {
    peer_set.Load();
    if (peer_set.is_loaded()) {
      for (auto const& peer : peer_set->peers) {
        if (!peer.remote_uid.empty()) {
          p2p_transport.Connect(peer.remote_uid);
        }
      }
    }
  }

  win_presenter.CreateNativeWindow();
  log_chat_journal();

  auto commit_chat_text = [&](std::string const& text) {
    chat_ui.SubmitText(text);
    chat.Save();
    app.Save();
    chat_ui.RefreshTranscript();
    emit_visible_keys();
    std::uint32_t event_id = 0;
    chat.Load();
    if (chat.is_loaded() && !chat->journal.empty()) {
      event_id = chat->journal.back().event.id().id();
    }
    LogLine("CHAT_MESSAGE_COMMITTED platform=windows event=" +
            std::to_string(event_id) + " text_key=" + text +
            " t_us=" + std::to_string(system_utc_micros()));
    LogLine("MESSAGE_COMMITTED text=" + text);
    log_chat_journal();
  };

  auto maybe_commit_inbox = [&]() {
    if (!options.commit_inbox.has_value()) {
      return;
    }
    auto const& inbox = *options.commit_inbox;
    std::error_code ec;
    if (!std::filesystem::exists(inbox, ec) || ec) {
      return;
    }
    std::ifstream in(inbox, std::ios::binary);
    if (!in) {
      return;
    }
    std::string line;
    if (!std::getline(in, line)) {
      in.close();
      std::filesystem::remove(inbox, ec);
      return;
    }
    in.close();
    std::filesystem::remove(inbox, ec);
    while (!line.empty() && (line.back() == char(13) || line.back() == char(10))) {
      line.pop_back();
    }
    if (line.empty()) {
      return;
    }
    commit_chat_text(line);
  };

  auto peer_already_present = [&](ae::Uid const& uid) {
    if (chat_sync.FindSession(uid) != nullptr) {
      return true;
    }
    peer_set.Load();
    if (!peer_set.is_loaded()) {
      return false;
    }
    for (auto const& peer : peer_set->peers) {
      if (peer.remote_uid == uid) {
        return true;
      }
    }
    return false;
  };

  auto maybe_peer_inbox = [&]() {
    if (!options.peer_inbox.has_value()) {
      return;
    }
    auto const& inbox = *options.peer_inbox;
    std::error_code ec;
    if (!std::filesystem::exists(inbox, ec) || ec) {
      return;
    }
    std::ifstream in(inbox, std::ios::binary);
    if (!in) {
      return;
    }
    std::string line;
    if (!std::getline(in, line)) {
      in.close();
      std::filesystem::remove(inbox, ec);
      return;
    }
    in.close();
    std::filesystem::remove(inbox, ec);
    while (!line.empty() && (line.back() == char(13) || line.back() == char(10))) {
      line.pop_back();
    }
    if (line.empty()) {
      return;
    }
    auto const uid = ae::Uid::FromString(std::string_view{line});
    if (uid.empty()) {
      LogLine("CHAT_PEER_INBOX_INVALID uid_text=" + line);
      return;
    }
    auto const uid_text = apptraverse::examples::FormatAetherUid(uid);
    if (peer_already_present(uid)) {
      LogLine("CHAT_PEER_ALREADY_PRESENT uid=" + uid_text);
      return;
    }
    chat_sync.AddPeer(uid);
    p2p_transport.Connect(uid);
    LogLine("CHAT_PEER_INBOX_ADDED uid=" + uid_text);
  };

  if (options.commit_message.has_value()) {
    commit_chat_text(*options.commit_message);
  }

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
    std::uint32_t event_id = 0;
    chat.Load();
    if (chat.is_loaded() && !chat->journal.empty()) {
      event_id = chat->journal.back().event.id().id();
    }
    LogLine("CHAT_MESSAGE_COMMITTED platform=windows event=" +
            std::to_string(event_id) +
            " text_key=" + *options.send_after_sync +
            " t_us=" + std::to_string(system_utc_micros()));
    LogLine("CHAT_SEND_AFTER_SYNC text=" + *options.send_after_sync);
  };

  auto finish = [&](int code) {
    chat_sync.Stop();
    return code;
  };

  for (;;) {
    int const quit_code = ProcessPendingWin32Messages();
    if (quit_code >= 0) {
      return finish(quit_code);
    }
    if (aether_app->IsExited()) {
      return finish(aether_app->ExitCode());
    }
    if (deadline_ptr != nullptr && ae::Now() >= *deadline_ptr) {
      std::cerr << "P2P ping timed out waiting for PONG\n";
      return finish(1);
    }
    if (options.wait_for_message.has_value() && saw_wait_message &&
        options.exit_after_message) {
      return finish(0);
    }
    if (options.exit_after_pending_clear && pending_cleared_after_commit) {
      app.Save();
      return finish(0);
    }

    auto const now = ae::Now();
    auto const next_update = aether_app->Update(now);
    chat_sync.Tick(now);
    maybe_commit_inbox();
    maybe_peer_inbox();
    maybe_send_after_sync();
    check_wait_message();

    if (options.exit_after_pending_clear &&
        options.commit_message.has_value()) {
      std::size_t pending_total = 0;
      peer_set.Load();
      if (peer_set.is_loaded()) {
        for (auto const& peer : peer_set->peers) {
          if (auto* session = chat_sync.FindSession(peer.remote_uid)) {
            pending_total += session->pending_packet_count();
          }
        }
      }
      if (pending_total > 0) {
        saw_pending_after_commit = true;
      } else if (saw_pending_after_commit) {
        pending_cleared_after_commit = true;
        LogLine("CHAT_EXIT_AFTER_PENDING_CLEAR");
      }
    }

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
