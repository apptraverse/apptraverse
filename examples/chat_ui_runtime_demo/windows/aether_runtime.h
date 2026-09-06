#ifndef APPTRAVERSE_CHAT_AETHER_RUNTIME_H_
#define APPTRAVERSE_CHAT_AETHER_RUNTIME_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "chat_presence.h"

namespace apptraverse {

// Runs AetherApp on its own thread. Owns all P2pStream objects.
// Model/UI threads only enqueue commands and receive UID-based callbacks.
// Local Presence only: DiagnoseLocalPresence on this thread; no remote
// QueryPeerPresence.
class ChatAetherRuntime {
 public:
  using UidCallback = std::function<void(std::string uid_text)>;
  using PresenceCallback = std::function<void(PresenceState state)>;
  using PeerReadyCallback = std::function<void(std::string remote_uid)>;
  using PeerClosedCallback = std::function<void(std::string remote_uid)>;
  using PeerFrameCallback =
      std::function<void(std::string remote_uid,
                         std::vector<std::uint8_t> bytes)>;
  using PeerWriteFailedCallback = std::function<void(std::string remote_uid)>;

  ChatAetherRuntime() = default;
  ~ChatAetherRuntime();

  ChatAetherRuntime(ChatAetherRuntime const&) = delete;
  ChatAetherRuntime& operator=(ChatAetherRuntime const&) = delete;

  void Start(std::filesystem::path aether_state_dir, UidCallback on_uid,
             PresenceCallback on_presence = {});
  void SetPeerCallbacks(PeerReadyCallback on_ready,
                        PeerClosedCallback on_closed,
                        PeerFrameCallback on_frame);
  void SetPeerWriteFailedCallback(PeerWriteFailedCallback on_write_failed);

  // Thread-safe: enqueue work for the Aether thread.
  void OpenPeer(std::string remote_uid);
  void SendPeerFrame(std::string remote_uid, std::vector<std::uint8_t> bytes);
  void ClosePeer(std::string remote_uid);

  void RequestStop();
  void Join();

 private:
  enum class CommandType : std::uint8_t {
    kOpenPeer = 1,
    kSendFrame = 2,
    kClosePeer = 3,
  };

  struct Command {
    CommandType type{CommandType::kOpenPeer};
    std::string remote_uid;
    std::vector<std::uint8_t> bytes;
  };

  void ThreadMain(std::filesystem::path aether_state_dir, UidCallback on_uid,
                  PresenceCallback on_presence);
  void Enqueue(Command command);

  std::atomic<bool> stop_{false};
  std::thread thread_;

  std::mutex command_mu_;
  std::queue<Command> commands_;

  std::mutex callback_mu_;
  PeerReadyCallback on_peer_ready_;
  PeerClosedCallback on_peer_closed_;
  PeerFrameCallback on_peer_frame_;
  PeerWriteFailedCallback on_peer_write_failed_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_AETHER_RUNTIME_H_
