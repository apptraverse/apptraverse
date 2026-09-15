#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aether_byte_transport.h"
#include "chat_aether_runtime.h"
#include "chat_presence.h"

namespace {

using apptraverse::example::chat_demo::AetherByteTransport;
using apptraverse::example::chat_demo::ChatAetherRuntime;
using apptraverse::example::chat_demo::PeerPresence;

char const* PresenceStateName(PeerPresence presence) {
  switch (presence) {
    case PeerPresence::kUnknown:
      return "unknown";
    case PeerPresence::kConnecting:
      return "connecting";
    case PeerPresence::kOnline:
      return "online";
    case PeerPresence::kOffline:
      return "offline";
  }
  return "unknown";
}

}  // namespace

int main(int argc, char* argv[]) {
  std::filesystem::path state_dir;
  std::string client_name;
  std::string peer_uid;
  std::uint64_t heartbeat_ms = 2000;
  std::uint64_t offline_ms = 7000;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--client-name" && i + 1 < argc) {
      client_name = argv[++i];
    } else if (arg == "--peer-uid" && i + 1 < argc) {
      peer_uid = argv[++i];
    } else if (arg == "--heartbeat-ms" && i + 1 < argc) {
      heartbeat_ms = std::stoull(argv[++i]);
    } else if (arg == "--offline-ms" && i + 1 < argc) {
      offline_ms = std::stoull(argv[++i]);
    }
  }

  if (state_dir.empty() || client_name.empty()) {
    std::cerr << "Usage: apptraverse_chat_aether_probe --state-dir <dir> "
                 "--client-name <name> [--peer-uid <uid>] [--heartbeat-ms <n>] "
                 "[--offline-ms <n>]\n";
    return 1;
  }

  ChatAetherRuntime runtime;
  std::unique_ptr<AetherByteTransport> transport;
  std::mutex out_mu;
  std::string local_uid;

  ChatAetherRuntime::Config config{
      .state_dir = state_dir,
      .client_name = client_name,
      .heartbeat_period_ms = heartbeat_ms,
      .offline_after_ms = offline_ms,
  };

  runtime.Start(
      std::move(config),
      /*on_uid=*/
      [&local_uid, &out_mu](std::string uid) {
        std::lock_guard<std::mutex> lock{out_mu};
        local_uid = std::move(uid);
      },
      /*on_ready=*/
      [&runtime, &transport, &out_mu, &local_uid, peer_uid]() {
        {
          std::lock_guard<std::mutex> lock{out_mu};
          transport = std::make_unique<AetherByteTransport>(runtime, local_uid);
          transport->BindReceive(
              nullptr,
              [](void*, std::string const& source,
                 std::vector<std::uint8_t> const& bytes) {
                std::string const text(bytes.begin(), bytes.end());
                std::cout << "RX peer=" << source << " bytes=" << bytes.size()
                          << " text=" << text << "\n"
                          << std::flush;
              });
          std::cout << "READY uid=" << local_uid << "\n" << std::flush;
        }
        if (!peer_uid.empty()) {
          runtime.OpenPeer(peer_uid);
        }
      },
      /*on_failed=*/
      [&out_mu](std::string error) {
        std::lock_guard<std::mutex> lock{out_mu};
        std::cerr << "FAILED error=" << error << "\n" << std::flush;
      },
      /*on_frame=*/
      [](std::string source_uid, std::vector<std::uint8_t> bytes) {
        // Will be delivered through transport->BindReceive if bound
        (void)source_uid;
        (void)bytes;
      },
      /*on_presence=*/
      [&out_mu](std::string peer, PeerPresence presence) {
        std::lock_guard<std::mutex> lock{out_mu};
        std::cout << "PRESENCE peer=" << peer
                  << " state=" << PresenceStateName(presence) << "\n"
                  << std::flush;
      });

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.rfind("send ", 0) == 0) {
      auto rest = line.substr(5);
      auto space_pos = rest.find(' ');
      if (space_pos != std::string::npos) {
        auto target_peer = rest.substr(0, space_pos);
        auto text = rest.substr(space_pos + 1);
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        runtime.Send(target_peer, std::move(bytes));
      }
    } else if (line == "quit" || line == "exit") {
      break;
    }
  }

  runtime.RequestStop();
  runtime.Join();
  return 0;
}
