#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_SESSION_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_SESSION_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
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
#include "chat_connectivity.h"
#include "chat_launch_options.h"
#include "chat_model.h"
#include "chat_presence.h"
#include "apptraverse/shared_event_id.h"

namespace apptraverse::example::chat_demo {

enum class SessionLifecycleState : std::uint8_t {
  kStarting = 0,
  kReady = 1,
  kFailed = 2,
  kStopped = 3,
};

enum class ChatPublicationKind : std::uint8_t {
  kInitial = 0,
  kStructural = 1,
};

struct ChatRuntimeStatus {
  std::string local_endpoint_uid;
  SessionLifecycleState lifecycle_state{SessionLifecycleState::kStarting};
  LocalConnectivityState local_connectivity{LocalConnectivityState::kUnknown};
  std::unordered_map<std::string, PeerPresence> remote_presence;
  std::unordered_map<std::string, RoomBootstrapState> room_bootstrap_by_peer_uid;
  std::map<SharedEventId, MessageDeliveryState> delivery_by_event_id;
  std::string error_text;
  std::uint64_t completed_checkpoint_id{0};
};

struct ChatUiUpdate {
  std::optional<std::vector<std::uint8_t>> publication_bytes;
  ChatPublicationKind kind{ChatPublicationKind::kInitial};
  std::uint64_t publication_serial{0};
  std::map<ae::ObjId, std::uint64_t> processed_edit_revisions_by_entry;
  std::optional<ae::ObjId> selected_chat_ack;
  ChatRuntimeStatus runtime_status;
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

  explicit ChatSession(EndpointFactory endpoint_factory = {});
  ~ChatSession();

  ChatSession(ChatSession const&) = delete;
  ChatSession& operator=(ChatSession const&) = delete;

  bool Start(ChatSessionConfig config, UiNotifyFn notify_ui);
  void RequestStop();
  void Join();

  void OpenPeer(OpenPeerRequest request);
  void SelectChat(ae::ObjId entry_id);
  void EditDraft(ae::ObjId entry_id, std::string text, std::uint64_t edit_revision);
  void SendDraft(ae::ObjId entry_id, std::string current_text,
                 std::uint64_t edit_revision);
  void SaveScroll(ae::ObjId entry_id, ScrollAnchor anchor);
  void SaveBounds(DesktopBounds bounds);
  void RetryConnection();
  bool Checkpoint(std::uint64_t request_id);

  std::optional<ChatUiUpdate> TryTakeUiUpdate();
  ChatRuntimeStatus GetRuntimeStatus() const;
  bool IsFinished() const noexcept;

 private:
  using ModelWork = std::function<void()>;

  struct PendingPublicationMetadata {
    ChatPublicationKind kind{ChatPublicationKind::kInitial};
    std::uint64_t serial{0};
    std::map<ae::ObjId, std::uint64_t> edit_revisions_by_entry;
    std::optional<ae::ObjId> selected_chat_ack;
  };

  void EnqueueUserModelWork(ModelWork work);
  void EnqueueInternalModelWork(ModelWork work);
  void ThreadMain(ChatSessionConfig config, UiNotifyFn notify_ui);

  void AssertModelThread() const;
  void UpdateStatus(std::function<void(ChatRuntimeStatus&)> mutator);
  void DrainModelQueue();
  void ShutdownEndpointAndDrain(IAetherFrameEndpoint* endpoint);

  EndpointFactory endpoint_factory_;
  PublicationChannel<3> channel_;
  UiNotifyFn notify_ui_;

  std::mutex publication_mu_;
  std::optional<PendingPublicationMetadata> pending_publication_metadata_;
  std::uint64_t next_publication_serial_{1};

  mutable std::mutex status_mu_;
  ChatRuntimeStatus status_;
  std::uint64_t status_serial_{0};
  std::uint64_t last_delivered_status_serial_{0};

  std::mutex queue_mu_;
  std::condition_variable queue_cv_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> finished_{false};
  bool started_{false};
  bool accepting_user_commands_{false};
  bool accepting_internal_delivery_{false};
  std::deque<ModelWork> work_queue_;
  std::thread worker_thread_;
  std::thread::id model_thread_id_{};

  std::function<void(OpenPeerRequest)> on_open_peer_;
  std::function<void(ae::ObjId)> on_select_chat_;
  std::function<void(ae::ObjId, std::string, std::uint64_t)> on_edit_draft_;
  std::function<void(ae::ObjId, std::string, std::uint64_t)> on_send_draft_;
  std::function<void(ae::ObjId, ScrollAnchor)> on_save_scroll_;
  std::function<void(DesktopBounds)> on_save_bounds_;
  std::function<void()> on_retry_connection_;
  std::function<void(std::uint64_t)> on_checkpoint_;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_SESSION_H_
