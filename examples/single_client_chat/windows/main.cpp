#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
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
#include "aether/config.h"

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
#include "../common/chat_component.h"
#include "../common/chat_transcript.h"
#include "../common/graph_builder.h"
#include "../common/runtime_jsonl.h"
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
  // Ordinary Windows chat auto-accepts the first valid incoming peer.
  bool auto_accept_peer{true};
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
    } else if (arg == "--no-auto-accept-peer") {
      options.auto_accept_peer = false;
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


#ifndef AE_GIT_VERSION
#  define AE_GIT_VERSION "unknown"
#endif
#define APPTRAVERSE_AE_STRINGIFY_HELPER(x) #x
#define APPTRAVERSE_AE_STRINGIFY(x) APPTRAVERSE_AE_STRINGIFY_HELPER(x)
bool VerboseLogEnabled() {
  static bool const enabled = [] {
    char const* raw = std::getenv("APPTRAVERSE_VERBOSE_LOG");
    if (raw == nullptr || raw[0] == '\0') {
      return false;
    }
    std::string value{raw};
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t')) {
      value.erase(value.begin());
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t' ||
            value.back() == '\r' || value.back() == '\n')) {
      value.pop_back();
    }
    for (char& ch : value) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value == "1" || value == "true" || value == "yes" ||
           value == "on";
  }();
  return enabled;
}

void LogLine(std::string const& line) {
  if (!VerboseLogEnabled()) {
    return;
  }
  std::cout << line << '\n';
  std::fflush(stdout);
}

using RuntimeField = apptraverse::examples::RuntimeJsonlLogger::Field;

char const* TimelineKindName(apptraverse::chat::ChatTimelineItemKind kind) {
  switch (kind) {
    case apptraverse::chat::ChatTimelineItemKind::kJoined:
      return "joined";
    case apptraverse::chat::ChatTimelineItemKind::kMessage:
      return "message";
  }
  return "unknown";
}

char const* DirectionName(apptraverse::chat::ChatMessageDirection direction) {
  switch (direction) {
    case apptraverse::chat::ChatMessageDirection::kLocal:
      return "local";
    case apptraverse::chat::ChatMessageDirection::kRemote:
      return "remote";
    case apptraverse::chat::ChatMessageDirection::kUnknown:
      return "unknown";
  }
  return "unknown";
}

void EmitPresentationEvent(
    apptraverse::examples::RuntimeJsonlLogger* runtime_log,
    apptraverse::chat::ChatComponent const& chat_component) {
  if (runtime_log == nullptr) {
    return;
  }
  auto const snap = chat_component.CapturePresentation();
  if (snap.timeline.empty()) {
    runtime_log->Emit(
        "presentation",
        {RuntimeField::UInt("timeline_count", snap.timeline.size()),
         RuntimeField::UInt("peer_count", snap.peers.size()),
         RuntimeField::Null("last_entry_kind"),
         RuntimeField::Null("last_entry_author"),
         RuntimeField::Null("last_entry_text"),
         RuntimeField::Null("last_event_obj_id")});
    return;
  }
  auto const& last = snap.timeline.back();
  runtime_log->Emit(
      "presentation",
      {RuntimeField::UInt("timeline_count", snap.timeline.size()),
       RuntimeField::UInt("peer_count", snap.peers.size()),
       RuntimeField::String("last_entry_kind", TimelineKindName(last.kind)),
       RuntimeField::String("last_entry_author", last.author.display_name),
       RuntimeField::String("last_entry_text", last.text),
       RuntimeField::UInt("last_event_obj_id", last.event_obj_id)});
}

void EmitMessageVisibleEvents(
    apptraverse::examples::RuntimeJsonlLogger* runtime_log,
    apptraverse::chat::ChatComponent const& chat_component,
    std::set<std::uint32_t>& emitted_event_obj_ids) {
  if (runtime_log == nullptr) {
    return;
  }
  auto const snap = chat_component.CapturePresentation();
  for (auto const& item : snap.timeline) {
    if (item.kind != apptraverse::chat::ChatTimelineItemKind::kMessage) {
      continue;
    }
    if (!emitted_event_obj_ids.insert(item.event_obj_id).second) {
      continue;
    }
    runtime_log->Emit(
        "message_visible",
        {RuntimeField::UInt("event_obj_id", item.event_obj_id),
         RuntimeField::String("author", item.author.display_name),
         RuntimeField::String("text", item.text),
         RuntimeField::String("direction", DirectionName(item.direction))});
  }
}

void EmitTextSubmitEvent(
    apptraverse::examples::RuntimeJsonlLogger* runtime_log,
    std::string const& text,
    std::optional<std::uint32_t> const& event_id) {
  if (runtime_log == nullptr) {
    return;
  }
  if (event_id.has_value()) {
    runtime_log->Emit(
        "text_submit",
        {RuntimeField::String("text", text),
         RuntimeField::Bool("accepted", true),
         RuntimeField::UInt("event_obj_id", *event_id)});
  } else {
    runtime_log->Emit(
        "text_submit",
        {RuntimeField::String("text", text),
         RuntimeField::Bool("accepted", false),
         RuntimeField::Null("event_obj_id")});
  }
}

void EmitPeerAddEvent(apptraverse::examples::RuntimeJsonlLogger* runtime_log,
                      std::string const& peer, bool accepted) {
  if (runtime_log == nullptr) {
    return;
  }
  runtime_log->Emit("peer_add",
                    {RuntimeField::String("peer", peer),
                     RuntimeField::Bool("accepted", accepted)});
}

int Run(CliOptions const& options) {
  auto runtime_log = apptraverse::examples::RuntimeJsonlLogger::TryOpenFromEnvironment();
  apptraverse::examples::RuntimeJsonlLogger* runtime_log_ptr = runtime_log.get();

  if (options.p2p_ping.has_value() && options.p2p_ping->empty()) {
    return 1;
  }
  if (options.peer.has_value() && options.peer->empty()) {
    return 1;
  }

  auto runtime = ConstructWindowsAetherApp(options.state_dir);
  auto aether_app = std::move(runtime.app);
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
  if (!app.is_loaded() || !app->window.is_valid()) {
    auto graph =
        apptraverse::examples::BuildSingleClientChatGraph<
            apptraverse::WindowsWindow, apptraverse::WinWindowPresenter,
            apptraverse::WinChatPresenter>(aether_app->domain(), "Windows");
    app = graph.app;
    if (!app.is_valid()) {
      std::cerr << "Failed to build App graph\n";
      return 1;
    }
    app.Save();
    LogLine("WINDOWS_GRAPH_CREATED");
  } else {
    LogLine("WINDOWS_GRAPH_LOADED");
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
      *aether_app, options.aether_client_name);
  if (!aether_client) {
    std::cerr << "Failed to select Aether client\n";
    return 1;
  }
  LogLine("AETHER_CLIENT_READY platform=windows uid=" +
          apptraverse::examples::FormatAetherUid(aether_client->uid()));
  auto const local_aether_uid =
      apptraverse::examples::FormatAetherUid(aether_client->uid());
  if (runtime_log_ptr != nullptr) {
    runtime_log_ptr->Emit("runtime_started",
                          {RuntimeField::String("local_uid", local_aether_uid)});
  }
  LogLine(std::string("AETHER_BUILD_INFO platform=windows git=") + AE_GIT_VERSION +
          " quarantine_ms=" + APPTRAVERSE_AE_STRINGIFY(AE_CLOUD_SERVER_QUARANTINE_TIME_MS) +
          " task_max=" + APPTRAVERSE_AE_STRINGIFY(AE_TASK_MAX_COUNT));
  if (options.print_aether_uid) {
    std::cout << "AETHER_UID="
              << apptraverse::examples::FormatAetherUid(aether_client->uid())
              << '\n';
    std::fflush(stdout);
    return 0;
  }

  apptraverse::examples::AetherP2pTransport p2p_transport;
  if (VerboseLogEnabled()) {
    p2p_transport.SetLogHandler([](std::string line) { LogLine(line); });
  }
  p2p_transport.Start(*aether_app, aether_client);

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
  std::set<std::uint32_t> emitted_visible_event_ids;

  auto system_utc_micros = []() -> std::int64_t {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  };

  auto sync_send = [&](ae::Uid const& peer, ae::ObjId packet_id,
                       apptraverse::SerializedSyncPacket const& bytes) {
    p2p_transport.Send(peer, bytes);
    if (VerboseLogEnabled()) {
      LogLine(
          "SYNC_TRANSPORT_WRITE peer=" +
          apptraverse::examples::FormatAetherUid(peer) +
          " packet=" + std::to_string(packet_id.id()) +
          " t_us=" + std::to_string(system_utc_micros()));
    }
  };
  auto presence_send = [&](ae::Uid const& peer,
                           std::vector<std::uint8_t> const& bytes) {
    p2p_transport.Send(peer, bytes);
  };

  apptraverse::chat::ChatComponent::LogFunction chat_log;
  if (VerboseLogEnabled()) {
    chat_log = LogLine;
  }
  apptraverse::chat::ChatComponent chat_component(
      apptraverse::SyncReplica{aether_app->domain(), *domain_storage,
                               chat.id()},
      local_client, chat, sync_send, presence_send,
      [&](ae::Uid const& remote_uid) { p2p_transport.Connect(remote_uid); },
      apptraverse::chat::ChatSyncTiming{}, options.auto_accept_peer,
      std::move(chat_log));

  auto transcript_contains = [&](std::string const& needle) {
    auto const transcript = apptraverse::examples::FormatChatPresentationUtf8(
        chat_component.CapturePresentation());
    return transcript.find(needle) != std::string::npos;
  };

  auto emit_visible_keys = [&]() {
    auto const transcript = apptraverse::examples::FormatChatPresentationUtf8(
        chat_component.CapturePresentation());
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

  chat_component.SubscribePresentationChanged([&]() {
    chat_ui.RenderPresentation(chat_component.CapturePresentation());
    emit_visible_keys();
    check_wait_message();
    EmitMessageVisibleEvents(runtime_log_ptr, chat_component,
                             emitted_visible_event_ids);
    EmitPresentationEvent(runtime_log_ptr, chat_component);
    app.Save();
  });
  chat_ui.SetSubmitTextHandler([&](std::string text) {
    auto const submitted = text;
    auto event_id = chat_component.SubmitText(std::move(text));
    EmitTextSubmitEvent(runtime_log_ptr, submitted, event_id);
    return event_id.has_value();
  });

  p2p_transport.SetReceiveHandler(
      [&](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
        if (apptraverse::examples::TryHandleP2pProbePayload(
                p2p_transport, peer, payload, LogLine, on_pong_received)) {
          return;
        }
        chat_component.Receive(peer, payload);
      });
  chat_component.Start();

  if (options.peer.has_value()) {
    auto const peer_result = chat_component.AddPeer(*options.peer);
    EmitPeerAddEvent(
        runtime_log_ptr,
        apptraverse::examples::FormatAetherUid(*options.peer),
        peer_result == apptraverse::chat::AddPeerResult::kAdded);
  }

  auto const local_aether_uid_for_ui = local_aether_uid;
  chat_ui.SetPeerUi(
      local_aether_uid_for_ui,
      [&](std::string const& remote_text) -> apptraverse::AddPeerUiResult {
        auto trimmed = remote_text;
        while (!trimmed.empty() &&
               (trimmed.front() == ' ' || trimmed.front() == '\t' ||
                trimmed.front() == '\r' || trimmed.front() == '\n')) {
          trimmed.erase(trimmed.begin());
        }
        while (!trimmed.empty() &&
               (trimmed.back() == ' ' || trimmed.back() == '\t' ||
                trimmed.back() == '\r' || trimmed.back() == '\n')) {
          trimmed.pop_back();
        }
        auto const uid = ae::Uid::FromString(std::string_view{trimmed});
        if (uid.empty()) {
          return apptraverse::AddPeerUiResult::Invalid;
        }
        if (uid == aether_client->uid()) {
          EmitPeerAddEvent(runtime_log_ptr,
                           apptraverse::examples::FormatAetherUid(uid), false);
          return apptraverse::AddPeerUiResult::Self;
        }
        auto const peer_result = chat_component.AddPeer(uid);
        auto const accepted =
            peer_result == apptraverse::chat::AddPeerResult::kAdded;
        EmitPeerAddEvent(runtime_log_ptr,
                         apptraverse::examples::FormatAetherUid(uid), accepted);
        app.Save();
        LogLine("CHAT_PEER_UI_ADDED platform=windows uid=" +
                apptraverse::examples::FormatAetherUid(uid));
        return apptraverse::AddPeerUiResult::Ok;
      });

  win_presenter.CreateNativeWindow();
  auto const initial_snapshot = chat_component.CapturePresentation();
  chat_ui.RenderPresentation(initial_snapshot);
  log_chat_journal();

  auto commit_chat_text = [&](std::string const& text) {
    auto const event_id = chat_component.SubmitText(text);
    EmitTextSubmitEvent(runtime_log_ptr, text, event_id);
    if (!event_id.has_value()) {
      return;
    }
    app.Save();
    LogLine("CHAT_MESSAGE_COMMITTED platform=windows event=" +
            std::to_string(*event_id) + " text_key=" + text +
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
    auto const uid_text = apptraverse::examples::FormatAetherUid(uid);
    auto const snap = chat_component.CapturePresentation();
    for (auto const& peer : snap.peers) {
      if (peer.remote_uid == uid_text) {
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
    chat_component.AddPeer(uid);
    LogLine("CHAT_PEER_INBOX_ADDED uid=" + uid_text);
  };

  if (options.commit_message.has_value()) {
    commit_chat_text(*options.commit_message);
  }

  check_wait_message();

  ae::TimePoint ping_deadline{};
  ae::TimePoint const* deadline_ptr = nullptr;
  if (options.p2p_ping.has_value()) {
    on_pong_received = [app_ptr = aether_app.get(), log_chat_journal]() {
      log_chat_journal();
      app_ptr->Exit(0);
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
    auto const snap = chat_component.CapturePresentation();
    bool sync_ready = false;
    if (options.peer.has_value()) {
      auto const peer_text =
          apptraverse::examples::FormatAetherUid(*options.peer);
      for (auto const& peer : snap.peers) {
        if (peer.remote_uid == peer_text && peer.initial_sync_complete) {
          sync_ready = true;
          break;
        }
      }
    } else {
      for (auto const& peer : snap.peers) {
        if (peer.initial_sync_complete) {
          sync_ready = true;
          break;
        }
      }
    }
    if (!sync_ready) {
      return;
    }
    auto const event_id = chat_component.SubmitText(*options.send_after_sync);
    EmitTextSubmitEvent(runtime_log_ptr, *options.send_after_sync, event_id);
    if (!event_id.has_value()) {
      return;
    }
    app.Save();
    sent_after_sync = true;
    LogLine("CHAT_MESSAGE_COMMITTED platform=windows event=" +
            std::to_string(*event_id) +
            " text_key=" + *options.send_after_sync +
            " t_us=" + std::to_string(system_utc_micros()));
    LogLine("CHAT_SEND_AFTER_SYNC text=" + *options.send_after_sync);
  };

  auto finish = [&](int code) {
    if (runtime_log_ptr != nullptr) {
      runtime_log_ptr->Emit("runtime_stopped");
    }
    chat_component.Stop();
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
    chat_component.Tick(now);
    maybe_commit_inbox();
    maybe_peer_inbox();
    maybe_send_after_sync();
    check_wait_message();
    EmitMessageVisibleEvents(runtime_log_ptr, chat_component,
                             emitted_visible_event_ids);

    if (options.exit_after_pending_clear &&
        options.commit_message.has_value()) {
      std::size_t pending_total = 0;
      auto const snap = chat_component.CapturePresentation();
      for (auto const& peer : snap.peers) {
        pending_total += peer.pending_packets;
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
