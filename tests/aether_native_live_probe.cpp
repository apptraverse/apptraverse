// Native-only live Aether transport probe (no ChatSession / SharedNode / GUI).
// Modes: --stream raw | --stream safe
// Stdin: OPEN <peerUID> | SEND <peerUID> <seq> <size> | STOP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/adapters/ethernet.h"
#include "aether/aether_app.h"
#include "aether/client.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_port_handle.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/config.h"
#include "aether/global_ids.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/stream_api/istream.h"
#include "aether/types/uid.h"
#include "aether/write_action/write_action.h"
#include "ae-numeric/percentile.h"

#include "apptraverse/directory_domain_storage.h"

namespace {

constexpr char const* kAetherParentUid =
    "3ac93165-3d37-4970-87a6-fa4ee27744e4";

constexpr ae::SafeStreamConfig kChatSafeStreamConfig{
    .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
    .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
    .max_repeat_count = 10,
    .wait_ack_timeout = std::chrono::seconds{5},
    .send_ack_timeout = std::chrono::seconds{0},
    .send_repeat_timeout = std::chrono::seconds{2},
};

enum class StreamMode : std::uint8_t { kRaw = 1, kSafe = 2 };

enum class CmdType : std::uint8_t { kOpen = 1, kSend = 2, kStop = 3 };

struct Command {
  CmdType type{CmdType::kOpen};
  std::string peer_uid;
  std::uint64_t seq{0};
  std::size_t size{0};
};

struct PendingWrite {
  std::uint64_t seq{0};
  std::vector<std::uint8_t> bytes;
};

struct TerminalNotice {
  std::uint64_t token{0};
  std::uint64_t seq{0};
  ae::WriteAction::Status status{ae::WriteAction::Status::kFail};
};

struct PeerChannel {
  std::string uid_text;
  ae::Uid uid;
  std::shared_ptr<ae::P2pStream> raw;
  std::unique_ptr<ae::P2pSafeStream> safe;
  ae::ByteIStream* active{nullptr};
  ae::Subscription data_sub;
  ae::Subscription update_sub;
  ae::Subscription write_sub;
  std::uint64_t next_token{1};
  std::uint64_t active_token{0};
  std::uint64_t active_seq{0};
  std::deque<PendingWrite> pending;
  std::optional<TerminalNotice> notice;
  bool notice_pending{false};
};

std::mutex g_out_mu;

void EmitLine(std::string const& line) {
  std::lock_guard<std::mutex> lock{g_out_mu};
  std::cout << line << '\n' << std::flush;
}

std::string HexEncode(std::vector<std::uint8_t> const& bytes) {
  static char const* kHex = "0123456789abcdef";
  std::string out;
  out.resize(bytes.size() * 2);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    out[i * 2] = kHex[bytes[i] >> 4];
    out[i * 2 + 1] = kHex[bytes[i] & 0x0f];
  }
  return out;
}

std::vector<std::uint8_t> MakePayload(std::uint64_t seq, std::size_t size) {
  std::vector<std::uint8_t> out(size);
  for (std::size_t i = 0; i < size; ++i) {
    // Deterministic mix with embedded zeros/non-ASCII.
    std::uint8_t v = static_cast<std::uint8_t>((seq * 131u + i * 17u) & 0xffu);
    if ((i % 17u) == 0) {
      v = 0;
    } else if ((i % 23u) == 0) {
      v = static_cast<std::uint8_t>(0x80u | (v & 0x7fu));
    }
    out[i] = v;
  }
  if (size >= 8) {
    for (int b = 0; b < 8; ++b) {
      out[b] = static_cast<std::uint8_t>((seq >> (8 * b)) & 0xffu);
    }
  }
  return out;
}

ae::AetherAppContext MakeContext(
    std::shared_ptr<std::filesystem::path> const& state_dir_holder) {
  ae::AetherAppContext context{[state_dir_holder] {
    return std::unique_ptr<ae::IDomainStorage>{
        std::make_unique<apptraverse::DirectoryDomainStorage>(
            *state_dir_holder)};
  }};
#if AE_DISTILLATION
  context = std::move(context).AddAdapterFactory(
      [](ae::AetherAppContext const& app_context) {
        return ae::EthernetAdapter::ptr::Create(
            ae::CreateWith{app_context.domain()}.with_id(
                ae::GlobalId::kEthernetAdapter),
            app_context.aether(), app_context.poller(),
            app_context.dns_resolver());
      });
#endif
  return context;
}

char const* StatusName(ae::WriteAction::Status st) {
  switch (st) {
    case ae::WriteAction::Status::kSuccess:
      return "Success";
    case ae::WriteAction::Status::kFail:
      return "Fail";
    case ae::WriteAction::Status::kStop:
      return "Stop";
  }
  return "Unknown";
}

void EmitStreamInfo(std::string const& peer, ae::ByteIStream* stream) {
  if (stream == nullptr) {
    return;
  }
  auto const info = stream->stream_info();
  std::ostringstream oss;
  oss << "STREAM_INFO peer=" << peer
      << " max_element_size=" << info.max_element_size
      << " rec_element_size=" << info.rec_element_size
      << " is_writable=" << (info.is_writable ? 1 : 0)
      << " link_state=" << static_cast<int>(info.link_state);
  EmitLine(oss.str());
}

bool ParseCommand(std::string const& line, Command& out) {
  if (line == "STOP") {
    out = Command{.type = CmdType::kStop};
    return true;
  }
  std::istringstream iss(line);
  std::string verb;
  iss >> verb;
  if (verb == "OPEN") {
    out.type = CmdType::kOpen;
    iss >> out.peer_uid;
    return !out.peer_uid.empty();
  }
  if (verb == "SEND") {
    out.type = CmdType::kSend;
    iss >> out.peer_uid >> out.seq >> out.size;
    return !out.peer_uid.empty() && out.size > 0;
  }
  return false;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::filesystem::path state_dir;
  std::string client_name;
  StreamMode mode = StreamMode::kSafe;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--client-name" && i + 1 < argc) {
      client_name = argv[++i];
    } else if (arg == "--stream" && i + 1 < argc) {
      std::string_view v{argv[++i]};
      if (v == "raw") {
        mode = StreamMode::kRaw;
      } else if (v == "safe") {
        mode = StreamMode::kSafe;
      } else {
        EmitLine("ERROR reason=bad_stream_mode");
        return 2;
      }
    }
  }

  if (state_dir.empty() || client_name.empty()) {
    std::cerr
        << "Usage: apptraverse_aether_native_live_probe --state-dir <dir> "
           "--client-name <name> --stream raw|safe\n";
    return 2;
  }

  std::mutex cmd_mu;
  std::queue<Command> commands;
  std::atomic<bool> stop{false};
  std::atomic<bool> ready{false};

  std::thread stdin_thread{[&] {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.empty()) {
        continue;
      }
      Command cmd;
      if (!ParseCommand(line, cmd)) {
        EmitLine("ERROR reason=bad_command line=" + line);
        continue;
      }
      if (cmd.type == CmdType::kStop) {
        std::lock_guard<std::mutex> lock{cmd_mu};
        commands.push(cmd);
        stop = true;
        break;
      }
      std::lock_guard<std::mutex> lock{cmd_mu};
      commands.push(std::move(cmd));
    }
    stop = true;
  }};

  int exit_code = 0;
  try {
    std::filesystem::create_directories(state_dir);
    auto state_holder = std::make_shared<std::filesystem::path>(state_dir);
    auto aether_app = ae::AetherApp::Construct(MakeContext(state_holder));

    ae::Client::ptr client;
    ae::Subscription select_sub;
    ae::Subscription inbound_sub;
    bool select_started = false;
    bool client_ready = false;
    bool select_failed = false;
    std::string select_error;
    std::unordered_map<std::string, PeerChannel> peers;

    auto bind_peer = [&](std::string const& peer_uid_text, ae::Uid uid,
                         std::shared_ptr<ae::P2pStream> raw, bool inbound) {
      auto& peer = peers[peer_uid_text];
      if (peer.active != nullptr) {
        EmitLine(std::string("ERROR reason=peer_already_bound peer=") +
                 peer_uid_text + (inbound ? " inbound=1" : " inbound=0"));
        return;
      }
      peer.uid_text = peer_uid_text;
      peer.uid = uid;
      peer.raw = std::move(raw);
      if (mode == StreamMode::kSafe) {
        peer.safe = std::make_unique<ae::P2pSafeStream>(
            *aether_app, kChatSafeStreamConfig, peer.raw);
        peer.active = peer.safe.get();
      } else {
        peer.active = peer.raw.get();
      }
      peer.data_sub = peer.active->out_data_event().Subscribe(
          [peer_uid_text](ae::DataBuffer const& data) {
            std::vector<std::uint8_t> bytes(data.begin(), data.end());
            std::ostringstream oss;
            oss << "RECEIVE src=" << peer_uid_text << " bytes=" << bytes.size()
                << " hex=" << HexEncode(bytes);
            EmitLine(oss.str());
          });
      peer.update_sub = peer.active->stream_update_event().Subscribe(
          [&peers, peer_uid_text]() {
            auto it = peers.find(peer_uid_text);
            if (it == peers.end() || it->second.active == nullptr) {
              return;
            }
            EmitStreamInfo(peer_uid_text, it->second.active);
          });
      EmitLine(std::string("PEER_BOUND peer=") + peer_uid_text +
               (inbound ? " inbound=1" : " inbound=0"));
      EmitStreamInfo(peer_uid_text, peer.active);
    };

    auto open_peer = [&](std::string const& peer_uid_text) {
      if (peers.count(peer_uid_text) && peers[peer_uid_text].active != nullptr) {
        EmitLine("ERROR reason=open_existing peer=" + peer_uid_text);
        return;
      }
      auto uid = ae::Uid::FromString(peer_uid_text);
      auto handle = client->message_stream_manager().CreatePort(uid);
      auto stream = std::make_shared<ae::P2pStream>(*aether_app, client.Load(),
                                                    uid, std::move(handle));
      bind_peer(peer_uid_text, uid, std::move(stream), /*inbound=*/false);
    };

    auto process_notice = [](PeerChannel& peer) {
      if (!peer.notice_pending || !peer.notice.has_value()) {
        return;
      }
      peer.notice_pending = false;
      auto notice = *peer.notice;
      peer.notice.reset();
      if (notice.token != peer.active_token) {
        return;
      }
      peer.write_sub.Reset();
      peer.active_token = 0;
      std::ostringstream oss;
      oss << "NATIVE_WRITE_RESULT peer=" << peer.uid_text
          << " seq=" << notice.seq << " token=" << notice.token
          << " status=" << StatusName(notice.status);
      EmitLine(oss.str());
    };

    auto try_start_write = [](PeerChannel& peer) {
      while (peer.active_token == 0 && peer.active != nullptr &&
             !peer.pending.empty()) {
        auto info = peer.active->stream_info();
        if (info.rec_element_size == 0 || !info.is_writable) {
          EmitStreamInfo(peer.uid_text, peer.active);
          return;
        }
        auto item = std::move(peer.pending.front());
        peer.pending.pop_front();
        if (info.max_element_size != 0 &&
            item.bytes.size() > info.max_element_size) {
          std::ostringstream oss;
          oss << "ERROR reason=oversize peer=" << peer.uid_text
              << " seq=" << item.seq << " bytes=" << item.bytes.size()
              << " max_element_size=" << info.max_element_size;
          EmitLine(oss.str());
          continue;
        }
        peer.active_token = peer.next_token++;
        peer.active_seq = item.seq;
        auto const token = peer.active_token;
        auto const seq = item.seq;
        ae::DataBuffer buffer{item.bytes.begin(), item.bytes.end()};
        auto& action = peer.active->Write(std::move(buffer));
        if (action.is_finished()) {
          EmitLine("ERROR reason=finished_before_subscribe peer=" +
                   peer.uid_text + " seq=" + std::to_string(seq));
          peer.notice = TerminalNotice{.token = token,
                                       .seq = seq,
                                       .status = ae::WriteAction::Status::kFail};
          peer.notice_pending = true;
          return;
        }
        PeerChannel* owner = &peer;
        peer.write_sub = action.status_event().Subscribe(
            [owner, token, seq](ae::WriteAction::Status status) {
              if (owner->active_token != token) {
                return;
              }
              owner->notice = TerminalNotice{
                  .token = token, .seq = seq, .status = status};
              owner->notice_pending = true;
            });
        return;
      }
    };

    auto parent = ae::Uid::FromString(std::string{kAetherParentUid});
    while (!stop && !aether_app->IsExited()) {
      if (!select_started) {
        select_started = true;
        auto& select =
            aether_app->aether()->SelectClient(parent, client_name);
        select_sub = select.result_event().Subscribe(
            [&](ae::Result<ae::Client::ptr, int> const& res) {
              if (!res) {
                select_failed = true;
                select_error =
                    "SelectClient failed code=" + std::to_string(res.error());
                return;
              }
              client = res.value();
              client_ready = true;
            });
      }

      if (select_failed) {
        EmitLine("ERROR reason=select_failed detail=" + select_error);
        exit_code = 1;
        break;
      }

      if (client_ready && !inbound_sub) {
        select_sub.Reset();
        static_cast<void>(client->cloud_connection());

        auto constexpr kPingInterval = std::chrono::seconds{1};
        auto constexpr kReceiveWindow = std::chrono::seconds{1};
        auto constexpr kOfflineTimeout = std::chrono::seconds{1};
        auto const conf =
            ae::RxTimingConf::Every(
                std::chrono::duration_cast<ae::Duration>(kPingInterval))
                .WithWindow(
                    std::chrono::duration_cast<ae::Duration>(kReceiveWindow));
        if (auto policy = client->connectivity_policy()) {
          policy->ResetRxTimings();
          policy->SetOfflineDetectionTimeout(
              std::chrono::duration_cast<ae::Duration>(kOfflineTimeout));
          policy->ConfigureRxTimings(ae::RequestPolicy::All{})
              .ForAllPriorities(conf);
          for (auto* server : client->cloud_connection().selected_servers()) {
            if (server != nullptr) {
              policy->ConfigureServerRxTiming(server->server_id(), conf,
                                              ae::Percentile::FromPercent(99.0));
            }
          }
        }

        aether_app->aether().Save();
        std::string const uid_text = ae::Format("{}", client->uid());
        inbound_sub =
            client->message_stream_manager().new_port_event().Subscribe(
                [&](ae::P2pPortHandle handle) {
                  auto const uid = handle.destination();
                  auto const peer_uid_text = ae::Format("{}", uid);
                  if (peers.count(peer_uid_text) &&
                      peers[peer_uid_text].active != nullptr) {
                    EmitLine("ERROR reason=inbound_keep_existing peer=" +
                             peer_uid_text);
                    return;
                  }
                  auto stream = std::make_shared<ae::P2pStream>(
                      *aether_app, client.Load(), uid, std::move(handle));
                  bind_peer(peer_uid_text, uid, std::move(stream),
                            /*inbound=*/true);
                });
        EmitLine("READY uid=" + uid_text + " stream=" +
                 std::string(mode == StreamMode::kRaw ? "raw" : "safe"));
        ready = true;
      }

      if (client_ready) {
        std::queue<Command> local;
        {
          std::lock_guard<std::mutex> lock{cmd_mu};
          local.swap(commands);
        }
        while (!local.empty()) {
          auto cmd = std::move(local.front());
          local.pop();
          if (cmd.type == CmdType::kStop) {
            stop = true;
            break;
          }
          if (cmd.type == CmdType::kOpen) {
            open_peer(cmd.peer_uid);
          } else if (cmd.type == CmdType::kSend) {
            auto& peer = peers[cmd.peer_uid];
            peer.uid_text = cmd.peer_uid;
            auto bytes = MakePayload(cmd.seq, cmd.size);
            peer.pending.push_back(
                PendingWrite{.seq = cmd.seq, .bytes = std::move(bytes)});
            EmitLine("SEND_QUEUED peer=" + cmd.peer_uid +
                     " seq=" + std::to_string(cmd.seq) +
                     " bytes=" + std::to_string(cmd.size));
            if (peer.active == nullptr) {
              open_peer(cmd.peer_uid);
            }
          }
        }
      }

      auto const now = ae::Now();
      auto next = aether_app->Update(now);
      if (client_ready) {
        for (auto& [uid, peer] : peers) {
          (void)uid;
          process_notice(peer);
          try_start_write(peer);
        }
      }
      auto const wake_cap = now + std::chrono::milliseconds{25};
      if (next > wake_cap) {
        next = wake_cap;
      }
      aether_app->WaitUntil(next);
    }

    for (auto& [uid, peer] : peers) {
      (void)uid;
      peer.write_sub.Reset();
      peer.data_sub.Reset();
      peer.update_sub.Reset();
      peer.safe.reset();
      peer.raw.reset();
      peer.active = nullptr;
    }
    peers.clear();
    inbound_sub.Reset();
    select_sub.Reset();
    if (client) {
      aether_app->aether().Save();
    }
    aether_app->Exit(0);
  } catch (std::exception const& ex) {
    EmitLine(std::string("ERROR reason=exception detail=") + ex.what());
    exit_code = 1;
  } catch (...) {
    EmitLine("ERROR reason=unknown_exception");
    exit_code = 1;
  }

  stop = true;
  if (stdin_thread.joinable()) {
    // stdin may still block; detach so process can exit after STOP.
    stdin_thread.detach();
  }
  EmitLine("STOPPED");
  (void)ready;
  return exit_code;
}
