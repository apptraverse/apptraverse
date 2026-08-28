#ifndef APPTRAVERSE_CHAT_AETHER_RUNTIME_H_
#define APPTRAVERSE_CHAT_AETHER_RUNTIME_H_

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace apptraverse {

// Runs AetherApp on its own thread. Loads existing client or registers once,
// then reports the ae::Uid text via on_uid (invoked from the Aether thread —
// caller must hop to the Model thread).
class ChatAetherRuntime {
 public:
  using UidCallback = std::function<void(std::string uid_text)>;
  using PresenceCallback = std::function<void(bool online)>;

  ChatAetherRuntime() = default;
  ~ChatAetherRuntime();

  ChatAetherRuntime(ChatAetherRuntime const&) = delete;
  ChatAetherRuntime& operator=(ChatAetherRuntime const&) = delete;

  void Start(std::filesystem::path aether_state_dir, UidCallback on_uid,
             PresenceCallback on_presence = {});
  void RequestStop();
  void Join();

 private:
  void ThreadMain(std::filesystem::path aether_state_dir, UidCallback on_uid,
                  PresenceCallback on_presence);

  std::atomic<bool> stop_{false};
  std::thread thread_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_AETHER_RUNTIME_H_
