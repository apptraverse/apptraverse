#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "aether_byte_transport.h"
#include "chat_aether_runtime.h"
#include "chat_presence.h"
#include "join_delivery_trace.h"

namespace {

using apptraverse::example::chat_demo::AetherByteTransport;
using apptraverse::example::chat_demo::ChatAetherRuntime;
using apptraverse::example::chat_demo::ModelTask;
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

}  // namespace

int main(int argc, char* argv[]) {
  std::filesystem::path state_dir;
  std::string client_name;
  std::string peer_uid;
  std::uint64_t heartbeat_ms = 2000;
  std::uint64_t offline_ms = 7000;
  bool binary_ladder = false;

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
    } else if (arg == "--binary-ladder") {
      binary_ladder = true;
    }
  }

  if (state_dir.empty() || client_name.empty()) {
    std::cerr << "Usage: apptraverse_chat_aether_probe --state-dir <dir> "
                 "--client-name <name> [--peer-uid <uid>] [--heartbeat-ms <n>] "
                 "[--offline-ms <n>] [--binary-ladder]\n";
    return 1;
  }

  // APPTRAVERSE_JOIN_TRACE alone is not enough; initialize before Start().
  apptraverse::example::chat_demo::EnableJoinDeliveryTraceFromEnv();

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
      [&runtime, &transport, &out_mu, &local_uid, peer_uid, binary_ladder]() {
        {
          std::lock_guard<std::mutex> lock{out_mu};
          transport = std::make_unique<AetherByteTransport>(
              runtime, local_uid,
              [](ModelTask task) {
                if (task) {
                  task();
                }
              });
          if (binary_ladder) {
            transport->BindReceive(
                nullptr,
                [](void*, std::string const& source,
                   std::vector<std::uint8_t> const& bytes) {
                  std::cout << "RECEIVE src=" << source
                            << " bytes=" << bytes.size()
                            << " hex=" << HexEncode(bytes) << "\n"
                            << std::flush;
                });
          } else {
            transport->BindReceive(
                nullptr,
                [](void*, std::string const& source,
                   std::vector<std::uint8_t> const& bytes) {
                  std::string const text(bytes.begin(), bytes.end());
                  std::cout << "RX peer=" << source << " bytes=" << bytes.size()
                            << " text=" << text << "\n"
                            << std::flush;
                });
          }
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
    if (binary_ladder) {
      if (line.rfind("OPEN ", 0) == 0) {
        runtime.OpenPeer(line.substr(5));
      } else if (line.rfind("SEND ", 0) == 0) {
        std::istringstream iss(line.substr(5));
        std::string target;
        std::uint64_t seq = 0;
        std::size_t size = 0;
        iss >> target >> seq >> size;
        if (!target.empty() && size > 0) {
          runtime.Send(target, MakePayload(seq, size));
          std::cout << "SEND_QUEUED peer=" << target << " seq=" << seq
                    << " bytes=" << size << "\n"
                    << std::flush;
        }
      } else if (line == "STOP" || line == "quit" || line == "exit") {
        break;
      }
    } else if (line.rfind("send ", 0) == 0) {
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
