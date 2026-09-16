#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_SESSION_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_SESSION_H_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "aether-objects/obj/obj_id.h"
#include "aether_frame_endpoint.h"
#include "apptraverse/publication_channel.h"
#include "chat_launch_options.h"
#include "chat_model.h"
#include "chat_presence.h"

namespace apptraverse::example::chat_demo {

enum class SessionLifecycleState : std::uint8_t {
  kStarting = 0,
  kReady = 1,
  kFailed = 2,
  kStopped = 3,
};

// Snapshot of runtime status copied alongside GUI publication.
struct ChatRuntimeStatus {
  std::string local_endpoint_uid;
  SessionLifecycleState lifecycle_state{SessionLifecycleState::kStarting};
  std::unordered_map<std::string, PeerPresence> remote_presence;
  std::string error_text;
  std::uint64_t processed_edit_revision{0};
};

struct ChatSessionConfig {
  std::filesystem::path state_dir;
  std::optional<OpenPeerRequest> initial_open_peer;
  std::string aether_client_name{"apptraverse-chat"};
};

class ChatSession {
 public:
  using UiNotifyFn = std::function<void()>;
  using EndpointFactory =
      std::function<std::unique_ptr<IAetherFrameEndpoint>()>;

  // Default factory constructs ChatAetherRuntime. Tests may inject a fake.
  explicit ChatSession(EndpointFactory endpoint_factory = {});
  ~ChatSession();

  ChatSession(ChatSession const&) = delete;
  ChatSession& operator=(ChatSession const&) = delete;

  // Called from GUI/host thread
  bool Start(ChatSessionConfig config, UiNotifyFn notify_ui);
  void RequestStop();
  void Join();

  void OpenPeer(OpenPeerRequest request);
  void SelectChat(ae::ObjId entry_id);
  void EditDraft(ae::ObjId entry_id, std::string text, std::uint64_t edit_revision);
  void SendDraft(ae::ObjId entry_id, std::string current_text, std::uint64_t edit_revision);
  void SaveScroll(ae::ObjId entry_id, ScrollAnchor anchor);
  void SaveBounds(DesktopBounds bounds);

  // Thread-safe publication and status consumption for GUI thread
  PublicationChannel<3>& publication_channel() { return channel_; }
  ChatRuntimeStatus GetRuntimeStatus();

 private:
  using ModelWork = std::function<void()>;

  void EnqueueModelWork(ModelWork work);
  void ThreadMain(ChatSessionConfig config, UiNotifyFn notify_ui);

  void UpdateStatus(std::function<void(ChatRuntimeStatus&)> mutator);

  EndpointFactory endpoint_factory_;
  PublicationChannel<3> channel_;
  UiNotifyFn notify_ui_;

  std::mutex status_mu_;
  ChatRuntimeStatus status_;

  std::mutex queue_mu_;
  std::condition_variable queue_cv_;
  bool stop_{false};
  bool started_{false};
  std::deque<ModelWork> work_queue_;
  std::thread worker_thread_;

  std::function<void(OpenPeerRequest)> on_open_peer_;
  std::function<void(ae::ObjId)> on_select_chat_;
  std::function<void(ae::ObjId, std::string, std::uint64_t)> on_edit_draft_;
  std::function<void(ae::ObjId, std::string, std::uint64_t)> on_send_draft_;
  std::function<void(ae::ObjId, ScrollAnchor)> on_save_scroll_;
  std::function<void(DesktopBounds)> on_save_bounds_;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_SESSION_H_
