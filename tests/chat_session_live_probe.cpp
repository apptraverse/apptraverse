// Portable ChatSession live probe: real ChatAetherRuntime + UI Domain consumer.
// Control protocol (stdin lines) is separate from production networking.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"

#include "aether_link.h"
#include "chat_build_info.h"
#include "chat_model.h"
#include "chat_session.h"
#include "join_delivery_trace.h"

namespace {

using apptraverse::example::chat_demo::ChatJoinPhase;
using apptraverse::example::chat_demo::ChatEntry;
using apptraverse::example::chat_demo::ChatPublicationKind;
using apptraverse::example::chat_demo::ChatRoom;
using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::ChatUiUpdate;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::DemoRole;
using apptraverse::example::chat_demo::SessionLifecycleState;

char const* JoinPhaseName(ChatJoinPhase phase) {
  switch (phase) {
    case ChatJoinPhase::kIdle:
      return "Idle";
    case ChatJoinPhase::kJoining:
      return "Joining";
    case ChatJoinPhase::kAccepted:
      return "Accepted";
    case ChatJoinPhase::kJoined:
      return "Joined";
    case ChatJoinPhase::kFailed:
      return "Failed";
  }
  return "Unknown";
}

constexpr int kMaxCommandBytes = 4096;
constexpr int kMaxOutstanding = 64;

struct UiMirror {
  std::unique_ptr<ae::RamDomainStorage> storage =
      std::make_unique<ae::RamDomainStorage>();
  std::unique_ptr<ae::Domain> domain;
  ChatWorkspace::ptr workspace;
};

void Emit(std::string_view line) {
  std::cout << "CHATPROBE:" << line << '\n';
  std::cout.flush();
}

void ApplyUiUpdate(UiMirror& ui, ChatUiUpdate const& update) {
  if (!update.publication_bytes.has_value() ||
      update.publication_bytes->empty()) {
    return;
  }
  auto const& bytes = *update.publication_bytes;
  apptraverse::ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  if (!ui.workspace.is_valid()) {
    ui.domain = std::make_unique<ae::Domain>(*ui.storage);
    auto root = apptraverse::LoadInitialPublication(in, *ui.domain, *ui.storage);
    if (!root || root->GetClassId() != ChatWorkspace::kClassId) {
      Emit("ERROR:initial publication class mismatch");
      return;
    }
    auto held = ui.domain->Find(root->obj_id);
    if (!held) {
      Emit("ERROR:initial publication Domain::Find failed");
      return;
    }
    ui.workspace =
        ChatWorkspace::ptr{ui.domain.get(), root->obj_id, {}, std::move(held)};
  } else {
    apptraverse::ApplyStructuralPublicationAndUpdatePresenters(
        in, *ui.domain, *ui.storage, *ui.workspace);
  }
}

std::string HexEncode(std::string_view bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(bytes.size() * 2);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    auto const b = static_cast<unsigned char>(bytes[i]);
    out[i * 2] = kHex[b >> 4];
    out[i * 2 + 1] = kHex[b & 0x0f];
  }
  return out;
}

std::string ScenarioText(unsigned scenario) {
  // Deterministic UTF-8: Cyrillic, emoji, newlines, quotes, angle brackets.
  switch (scenario % 5) {
    case 0:
      return "probe-" + std::to_string(scenario) + " привет <tag> \"q\"";
    case 1:
      return "probe-" + std::to_string(scenario) + " line1\nline2 😀";
    case 2:
      return "probe-" + std::to_string(scenario) + " <> & 'quotes'";
    case 3:
      return "probe-" + std::to_string(scenario) + " мир/world";
    default:
      return "probe-" + std::to_string(scenario) + " final";
  }
}

ChatEntry::ptr SelectedOrFirst(ChatWorkspace& ws) {
  if (ws.selected_chat_id.is_valid()) {
    for (auto const& entry : ws.chats) {
      if (entry.is_valid() && entry.id() == ws.selected_chat_id) {
        return entry;
      }
    }
  }
  for (auto const& entry : ws.chats) {
    if (entry.is_valid()) {
      return entry;
    }
  }
  return {};
}

void EmitSnapshot(UiMirror const& ui, ChatSession const& session,
                  std::uint64_t request_id) {
  auto status = session.GetRuntimeStatus();
  std::ostringstream oss;
  oss << "SNAPSHOT request=" << request_id
      << " uid=" << status.local_endpoint_uid
      << " life=" << static_cast<int>(status.lifecycle_state)
      << " err=" << HexEncode(status.error_text);
  if (ui.workspace.is_valid()) {
    auto entry = SelectedOrFirst(*ui.workspace);
    if (entry.is_valid()) {
      oss << " entry=" << entry.id().id()
          << " draft=" << HexEncode(entry->draft)
          << " selected=" << ui.workspace->selected_chat_id.id();
      if (entry->room.is_valid()) {
        if (!entry->room.is_loaded()) {
          entry->room.Load();
        }
        oss << " room=" << entry->room.id().id()
            << " messages=" << entry->room->messages.size();
        for (auto const& msg : entry->room->messages) {
          Emit(std::string("MESSAGE uid=") + msg.id.origin_uid +
               " seq=" + std::to_string(msg.id.origin_sequence) +
               " ts=" + std::to_string(msg.timestamp_us) +
               " text=" + HexEncode(msg.text));
        }
      }
      if (entry->peer_link.is_valid()) {
        oss << " peer=" << entry->peer_link->EndpointUid();
      }
    }
  }
  Emit(oss.str());
}

}  // namespace

int main(int argc, char* argv[]) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  int build_info_exit = 0;
  if (apptraverse::example::chat_demo::TryHandleBuildInfoArgs(argc, argv,
                                                              &build_info_exit)) {
    return build_info_exit;
  }

  std::filesystem::path state_dir;
  DemoRole role = DemoRole::kHost;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--host") {
      role = DemoRole::kHost;
    } else if (arg == "--client") {
      role = DemoRole::kClient;
    }
  }
  if (state_dir.empty()) {
    std::cerr << "Usage: apptraverse_chat_session_live_probe --host|--client --state-dir <dir>\n"
                 "       stdin commands: JOIN/SEND/DRAFT/SNAPSHOT/CHECKPOINT/"
                 "RETRY/STOP\n";
    return 1;
  }

  apptraverse::example::chat_demo::EnableJoinDeliveryTraceFromEnv();

  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();
  apptraverse::example::chat_demo::EnsureAetherLinkRegistration();

  auto const info = apptraverse::example::chat_demo::GetChatBuildInfo();
  Emit(std::string("BUILD source=") + info.source_sha +
       " dirty=" + info.source_dirty_flag + " config=" + info.configuration +
       " fp=" + info.compile_fingerprint);

  UiMirror ui;
  ChatSession session;
  if (!session.Start(ChatSessionConfig{.state_dir = state_dir, .role = role}, [] {})) {
    Emit("ERROR:Start failed");
    return 1;
  }

  auto const ready_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (std::chrono::steady_clock::now() < ready_deadline) {
    while (auto update = session.TryTakeUiUpdate()) {
      ApplyUiUpdate(ui, *update);
    }
    auto status = session.GetRuntimeStatus();
    if (status.lifecycle_state == SessionLifecycleState::kReady &&
        !status.local_endpoint_uid.empty()) {
      Emit(std::string("READY uid=") + status.local_endpoint_uid);
      break;
    }
    if (status.lifecycle_state == SessionLifecycleState::kFailed) {
      Emit(std::string("ERROR:Ready failed ") + HexEncode(status.error_text));
      session.RequestStop();
      while (!session.IsFinished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      session.Join();
      Emit("STOPPED");
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (session.GetRuntimeStatus().lifecycle_state !=
      SessionLifecycleState::kReady) {
    Emit("ERROR:Ready timeout");
    session.RequestStop();
    while (!session.IsFinished()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    session.Join();
    Emit("STOPPED");
    return 1;
  }

  std::atomic<bool> stop_pump{false};
  std::mutex ui_mu;
  std::thread pump([&] {
    std::optional<std::uint32_t> last_room_id;
    ChatJoinPhase last_phase = ChatJoinPhase::kIdle;
    while (!stop_pump.load(std::memory_order_relaxed)) {
      bool emitted = false;
      {
        std::lock_guard<std::mutex> lock{ui_mu};
        while (auto update = session.TryTakeUiUpdate()) {
          ApplyUiUpdate(ui, *update);
          if (update->runtime_status.join_phase != last_phase) {
            last_phase = update->runtime_status.join_phase;
            Emit(std::string("JOINPHASE phase=") + JoinPhaseName(last_phase) +
                 " text=" + update->runtime_status.join_status_text);
            emitted = true;
          }
          if (ui.workspace.is_valid()) {
            auto entry = SelectedOrFirst(*ui.workspace);
            if (entry.is_valid() && entry->room.is_valid()) {
              auto const id = entry->room.id().id();
              if (!last_room_id.has_value() || *last_room_id != id) {
                last_room_id = id;
                Emit(std::string("ROOM id=") + std::to_string(id) +
                     (entry->peer_link.is_valid()
                          ? (" peer=" + entry->peer_link->EndpointUid())
                          : std::string{}));
                emitted = true;
              }
            }
          }
        }
      }
      if (!emitted) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
  });

  std::string line;
  int outstanding = 0;
  while (std::getline(std::cin, line)) {
    if (static_cast<int>(line.size()) > kMaxCommandBytes) {
      Emit("ERROR:command too long");
      continue;
    }

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    std::unique_lock<std::mutex> lock{ui_mu};
    if (cmd == "JOIN" || cmd == "OPEN") {
      std::string peer_uid;
      iss >> peer_uid;
      if (peer_uid.empty()) {
        Emit("ERROR:JOIN missing uid");
        continue;
      }
      session.SetHostUidInput(peer_uid);
      session.JoinHost();
    } else if (cmd == "SEND") {
      unsigned scenario = 0;
      iss >> scenario;
      if (!ui.workspace.is_valid()) {
        Emit("ERROR:SEND no workspace");
        continue;
      }
      auto entry = SelectedOrFirst(*ui.workspace);
      if (!entry.is_valid()) {
        Emit("ERROR:SEND no entry");
        continue;
      }
      auto text = ScenarioText(scenario);
      session.EditDraft(entry.id(), text, scenario + 1);
      session.SendDraft(entry.id(), text, scenario + 1);
      ++outstanding;
      if (outstanding > kMaxOutstanding) {
        Emit("ERROR:too many outstanding");
      }
    } else if (cmd == "DRAFT") {
      unsigned scenario = 0;
      std::uint64_t revision = 0;
      iss >> scenario >> revision;
      if (!ui.workspace.is_valid()) {
        Emit("ERROR:DRAFT no workspace");
        continue;
      }
      auto entry = SelectedOrFirst(*ui.workspace);
      if (!entry.is_valid()) {
        Emit("ERROR:DRAFT no entry");
        continue;
      }
      session.EditDraft(entry.id(), ScenarioText(scenario), revision);
    } else if (cmd == "SNAPSHOT") {
      std::uint64_t request_id = 0;
      iss >> request_id;
      EmitSnapshot(ui, session, request_id);
    } else if (cmd == "CHECKPOINT") {
      std::uint64_t request_id = 0;
      iss >> request_id;
      if (!session.Checkpoint(request_id)) {
        Emit("ERROR:CHECKPOINT not accepted");
        continue;
      }
      auto const cp_deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(30);
      while (std::chrono::steady_clock::now() < cp_deadline) {
        while (auto update = session.TryTakeUiUpdate()) {
          ApplyUiUpdate(ui, *update);
        }
        if (session.GetRuntimeStatus().completed_checkpoint_id >= request_id) {
          Emit(std::string("CHECKPOINT id=") + std::to_string(request_id));
          break;
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        lock.lock();
      }
    } else if (cmd == "RETRY") {
      session.RetryConnection();
    } else if (cmd == "STOP") {
      break;
    } else if (!cmd.empty()) {
      Emit(std::string("ERROR:unknown command ") + cmd);
    }
  }

  stop_pump.store(true, std::memory_order_relaxed);
  if (pump.joinable()) {
    pump.join();
  }

  session.RequestStop();
  auto const stop_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!session.IsFinished() &&
         std::chrono::steady_clock::now() < stop_deadline) {
    while (auto update = session.TryTakeUiUpdate()) {
      ApplyUiUpdate(ui, *update);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  session.Join();
  Emit("STOPPED");
  return session.IsFinished() ? 0 : 1;
}
