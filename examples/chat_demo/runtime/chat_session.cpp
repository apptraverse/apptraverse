#include "chat_session.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/registry.h"
#include "aether/types/uid.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync_runtime.h"

#include "aether_byte_transport.h"
#include "aether_link.h"
#include "chat_aether_runtime.h"
#include "chat_bootstrap.h"
#include "chat_command_limits.h"
#include "chat_commands.h"
#include "chat_demo_runtime_state.h"

namespace apptraverse::example::chat_demo {
namespace {

inline constexpr ae::ObjId kLocalWorkspaceRootId{10001};

std::string TrimAsciiWhitespace(std::string_view sv) {
  auto start = sv.begin();
  while (start != sv.end() &&
         std::isspace(static_cast<unsigned char>(*start))) {
    ++start;
  }
  auto end = sv.end();
  while (end != start &&
         std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }
  return std::string(start, end);
}

bool IsAsciiHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

// Validate user/CLI UID text before any assert-taking Aether string path.
bool TryCanonicalizeAetherUid(std::string_view raw, std::string& out) {
  std::string const trimmed = TrimAsciiWhitespace(raw);
  if (trimmed.size() != 36) {
    return false;
  }
  bool all_zero = true;
  for (std::size_t i = 0; i < trimmed.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (trimmed[i] != '-') {
        return false;
      }
      continue;
    }
    if (!IsAsciiHex(trimmed[i])) {
      return false;
    }
    if (trimmed[i] != '0') {
      all_zero = false;
    }
  }
  if (all_zero) {
    return false;
  }
  ae::UidString const uid_str{std::string_view{trimmed}};
  if (!uid_str.valid) {
    return false;
  }
  auto const uid = ae::Uid::FromString(uid_str);
  if (uid.empty()) {
    return false;
  }
  out = ae::Format("{}", uid);
  return !out.empty();
}

ChatEntry::ptr FindEntryById(ChatWorkspace& workspace, ae::ObjId entry_id) {
  for (auto const& entry : workspace.chats) {
    if (entry.is_valid() && entry.id() == entry_id) {
      return entry;
    }
  }
  return {};
}

struct RoomSyncSnapshot {
  apptraverse::InitialSyncPhase phase{
      apptraverse::InitialSyncPhase::NotStarted};
  bool has_pending_event{false};
  ae::ObjId pending_initial_packet_id;
  ae::ObjId pending_event_packet_id;
  std::size_t message_count{0};
  std::size_t journal_size{0};

  bool operator==(RoomSyncSnapshot const& other) const {
    return phase == other.phase && has_pending_event == other.has_pending_event &&
           pending_initial_packet_id == other.pending_initial_packet_id &&
           pending_event_packet_id == other.pending_event_packet_id &&
           message_count == other.message_count &&
           journal_size == other.journal_size;
  }
};

RoomBootstrapState BootstrapFromPhase(apptraverse::InitialSyncPhase phase) {
  switch (phase) {
    case apptraverse::InitialSyncPhase::Complete:
      return RoomBootstrapState::kComplete;
    case apptraverse::InitialSyncPhase::Pending:
      return RoomBootstrapState::kPending;
    case apptraverse::InitialSyncPhase::NotStarted:
    default:
      return RoomBootstrapState::kNotStarted;
  }
}

MessageDeliveryState DeliveryForOwnEvent(apptraverse::LinkSyncState& state,
                                         SharedEventId const& id,
                                         std::string const& local_uid) {
  if (id.origin_uid != local_uid || id.origin_sequence == 0) {
    return MessageDeliveryState::kNone;
  }
  if (state.HasDelivered(id)) {
    return MessageDeliveryState::kDelivered;
  }
  if (state.HasPendingEvent() && state.pending_event_identity == id) {
    return MessageDeliveryState::kSending;
  }
  return MessageDeliveryState::kQueued;
}

RoomSyncSnapshot CaptureRoomSyncSnapshot(ChatRoom& room,
                                         apptraverse::LinkSyncState& state) {
  RoomSyncSnapshot snapshot;
  snapshot.phase = state.GetInitialSyncPhase();
  snapshot.has_pending_event = state.HasPendingEvent();
  snapshot.pending_initial_packet_id = state.pending_initial_packet_id;
  snapshot.pending_event_packet_id = state.pending_event_packet_id;
  snapshot.message_count = room.messages.size();
  snapshot.journal_size = room.journal.size();
  return snapshot;
}

// Owns model Domain/storage and network objects for one ChatSession worker
// lifetime. Kept alive outside Initialize/Run try so catch paths never drain
// callbacks after Domain/storage have already unwound.
struct WorkerState {
  apptraverse::DirectoryDomainStorage storage;
  ae::Domain domain;
  ae::RamDomainStorage runtime_storage;
  ae::Domain runtime_domain;
  ChatWorkspace::ptr workspace;
  ChatDemoRuntimeState::ptr runtime;
  std::unique_ptr<IAetherFrameEndpoint> endpoint;
  std::unique_ptr<AetherByteTransport> transport;
  std::unique_ptr<SharedSyncRuntime> sync_runtime;
  std::string my_uid;
  bool aether_ready{false};
  bool identity_conflict{false};
  std::uint64_t network_epoch{0};
  std::unordered_map<std::string, PeerPresence> remote_presence_map;
  std::map<ae::ObjId, std::string> endpoint_by_pending_entry;
  std::map<std::string, ae::ObjId> waiting_entry_by_endpoint;
  bool publication_dirty{false};
  std::map<ae::ObjId, std::uint64_t> processed_edit_revisions;
  std::optional<ae::ObjId> pending_select_ack;
  IAetherFrameEndpoint::Config network_cfg{};
  bool network_retry_in_progress{false};
  std::function<void(std::string, std::vector<std::uint8_t>)> handle_control;

  // Model-thread join send scheduler (nonpersistent; one active attempt).
  ae::ObjId join_sched_attempt_id;
  std::vector<std::uint8_t> join_frozen_bytes;
  std::chrono::steady_clock::time_point join_attempt_start{};
  std::optional<std::chrono::steady_clock::time_point> join_last_send;
  std::uint64_t last_projected_runtime_gen{0};
  std::uint64_t last_projected_workspace_gen{0};
  ChatJoinPhase last_projected_join_phase{ChatJoinPhase::kIdle};
  std::string last_projected_join_error;
  std::string last_projected_host_input;
  std::string last_projected_local_uid;

  explicit WorkerState(std::filesystem::path const& model_dir)
      : storage{model_dir}, domain{storage}, runtime_domain{runtime_storage} {}
};

}  // namespace

ChatSession::ChatSession(EndpointFactory endpoint_factory)
    : endpoint_factory_(endpoint_factory
                            ? std::move(endpoint_factory)
                            : EndpointFactory{[] {
                                return std::make_unique<ChatAetherRuntime>();
                              }}) {}

ChatSession::~ChatSession() {
  RequestStop();
  Join();
}

bool ChatSession::Start(ChatSessionConfig config, UiNotifyFn notify_ui) {
  std::lock_guard<std::mutex> lock{queue_mu_};
  if (started_) {
    return false;
  }
  started_ = true;
  stop_requested_.store(false, std::memory_order_release);
  finished_.store(false, std::memory_order_release);
  accepting_user_commands_ = true;
  accepting_internal_delivery_ = true;
  notify_ui_ = std::move(notify_ui);
  worker_thread_ = std::thread([this, config = std::move(config), notify = notify_ui_]() mutable {
    ThreadMain(std::move(config), std::move(notify));
  });
  return true;
}

void ChatSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{queue_mu_};
    if (!started_ || stop_requested_.load(std::memory_order_relaxed)) {
      return;
    }
    stop_requested_.store(true, std::memory_order_release);
    accepting_user_commands_ = false;
  }
  queue_cv_.notify_all();
}

void ChatSession::Join() {
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

bool ChatSession::IsFinished() const noexcept {
  return finished_.load(std::memory_order_acquire);
}

void ChatSession::EnqueueUserModelWork(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{queue_mu_};
    if (!accepting_user_commands_) {
      return;
    }
    work_queue_.push_back(std::move(work));
  }
  queue_cv_.notify_all();
}

void ChatSession::EnqueueInternalModelWork(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{queue_mu_};
    if (!accepting_internal_delivery_) {
      return;
    }
    work_queue_.push_back(std::move(work));
  }
  queue_cv_.notify_all();
}

ChatRuntimeStatus ChatSession::GetRuntimeStatus() const {
  std::lock_guard<std::mutex> lock{status_mu_};
  return status_;
}

std::optional<ChatUiUpdate> ChatSession::TryTakeUiUpdate() {
  ChatUiUpdate update;
  bool have_publication = false;

  {
    std::lock_guard<std::mutex> pub_lock{publication_mu_};
    if (channel_.has_unread_published()) {
      auto bytes = channel_.TakePublishedCopy();
      assert(pending_publication_metadata_.has_value() &&
             "published slot requires matching metadata");
      PendingPublicationMetadata const meta = *pending_publication_metadata_;
      pending_publication_metadata_.reset();

      update.publication_bytes = std::move(bytes);
      update.kind = meta.kind;
      update.publication_serial = meta.serial;
      update.processed_edit_revisions_by_entry =
          std::move(meta.edit_revisions_by_entry);
      update.selected_chat_ack = meta.selected_chat_ack;
      have_publication = true;
    }
  }

  bool have_status = false;
  {
    std::lock_guard<std::mutex> status_lock{status_mu_};
    if (status_serial_ != last_delivered_status_serial_) {
      last_delivered_status_serial_ = status_serial_;
      update.runtime_status = status_;
      update.copy_host_uid = status_.pending_copy;
      status_.pending_copy.reset();
      have_status = true;
    } else if (have_publication) {
      update.runtime_status = status_;
      update.copy_host_uid = status_.pending_copy;
      status_.pending_copy.reset();
      have_status = true;
    }
  }

  if (!have_publication && !have_status) {
    return std::nullopt;
  }

  // Wake the model worker if it was retaining a dirty publication.
  queue_cv_.notify_all();
  return update;
}

void ChatSession::AssertModelThread() const {
#ifndef NDEBUG
  assert(std::this_thread::get_id() == model_thread_id_ &&
         "ChatSession model command must run on model thread");
#endif
}

void ChatSession::UpdateStatus(std::function<void(ChatRuntimeStatus&)> mutator) {
  {
    std::lock_guard<std::mutex> lock{status_mu_};
    mutator(status_);
    ++status_serial_;
  }
  if (notify_ui_) {
    notify_ui_();
  }
}

void ChatSession::DrainModelQueue() {
  for (;;) {
    std::deque<ModelWork> local_work;
    {
      std::lock_guard<std::mutex> lock{queue_mu_};
      if (work_queue_.empty()) {
        break;
      }
      local_work.swap(work_queue_);
    }
    while (!local_work.empty()) {
      auto work = std::move(local_work.front());
      local_work.pop_front();
      if (work) {
        work();
      }
    }
  }
}

void ChatSession::DiscardModelQueue() {
  std::lock_guard<std::mutex> lock{queue_mu_};
  work_queue_.clear();
  accepting_user_commands_ = false;
  accepting_internal_delivery_ = false;
}

void ChatSession::StopEndpointJoin(IAetherFrameEndpoint* endpoint) {
  if (endpoint != nullptr) {
    endpoint->RequestStop();
    endpoint->Join();
  }
}

void ChatSession::ShutdownEndpointAndDrain(IAetherFrameEndpoint* endpoint) {
  StopEndpointJoin(endpoint);
  DrainModelQueue();
}

void ChatSession::SetHostUidInput(std::string text) {
  EnqueueUserModelWork([this, text = std::move(text)]() mutable {
    AssertModelThread();
    if (on_set_host_uid_input_) {
      on_set_host_uid_input_(std::move(text));
    }
  });
}

void ChatSession::JoinHost() {
  EnqueueUserModelWork([this]() {
    AssertModelThread();
    if (on_join_host_) {
      on_join_host_();
    }
  });
}

void ChatSession::RequestCopyHostUid() {
  EnqueueUserModelWork([this]() {
    AssertModelThread();
    if (on_copy_host_uid_) {
      on_copy_host_uid_();
    }
  });
}

void ChatSession::SelectChat(ae::ObjId entry_id) {
  EnqueueUserModelWork([this, entry_id]() {
    AssertModelThread();
    if (on_select_chat_) {
      on_select_chat_(entry_id);
    }
  });
}

void ChatSession::EditDraft(ae::ObjId entry_id, std::string text,
                            std::uint64_t edit_revision) {
  EnqueueUserModelWork([this, entry_id, text = std::move(text), edit_revision]() mutable {
    AssertModelThread();
    if (on_edit_draft_) {
      on_edit_draft_(entry_id, std::move(text), edit_revision);
    }
  });
}

void ChatSession::SendDraft(ae::ObjId entry_id, std::string current_text,
                            std::uint64_t edit_revision) {
  EnqueueUserModelWork([this, entry_id, current_text = std::move(current_text),
                        edit_revision]() mutable {
    AssertModelThread();
    if (on_send_draft_) {
      on_send_draft_(entry_id, std::move(current_text), edit_revision);
    }
  });
}

void ChatSession::SaveScroll(ae::ObjId entry_id, ScrollAnchor anchor) {
  EnqueueUserModelWork([this, entry_id, anchor]() {
    AssertModelThread();
    if (on_save_scroll_) {
      on_save_scroll_(entry_id, anchor);
    }
  });
}

void ChatSession::SaveBounds(DesktopBounds bounds) {
  EnqueueUserModelWork([this, bounds]() {
    AssertModelThread();
    if (on_save_bounds_) {
      on_save_bounds_(bounds);
    }
  });
}

void ChatSession::RetryConnection() {
  // Never Join/Start on the caller (GUI/JNI) thread.
  EnqueueUserModelWork([this]() {
    AssertModelThread();
    if (on_retry_connection_) {
      on_retry_connection_();
    }
  });
}

bool ChatSession::Checkpoint(std::uint64_t request_id) {
  if (request_id == 0) {
    return false;
  }
  bool accepted = false;
  {
    std::lock_guard<std::mutex> lock{queue_mu_};
    accepted = accepting_user_commands_;
  }
  if (!accepted) {
    return false;
  }
  EnqueueUserModelWork([this, request_id]() {
    AssertModelThread();
    if (on_checkpoint_) {
      on_checkpoint_(request_id);
    }
  });
  return true;
}

void ChatSession::ThreadMain(ChatSessionConfig config, UiNotifyFn notify_ui) {
  model_thread_id_ = std::this_thread::get_id();

  bool worker_failed = false;
  std::string worker_error;
  std::unique_ptr<WorkerState> worker;

  auto const finalize_worker = [this, &worker_failed, &worker]() {
    on_set_host_uid_input_ = nullptr;
    on_join_host_ = nullptr;
    on_copy_host_uid_ = nullptr;
    on_select_chat_ = nullptr;
    on_edit_draft_ = nullptr;
    on_send_draft_ = nullptr;
    on_save_scroll_ = nullptr;
    on_save_bounds_ = nullptr;
    on_retry_connection_ = nullptr;
    on_checkpoint_ = nullptr;

    if (worker) {
      worker->sync_runtime.reset();
      worker->transport.reset();
      worker->endpoint.reset();
      worker->workspace = {};
      worker.reset();
    }

    {
      std::lock_guard<std::mutex> lock{queue_mu_};
      started_ = false;
    }

    if (!worker_failed) {
      UpdateStatus([](ChatRuntimeStatus& s) {
        if (s.lifecycle_state != SessionLifecycleState::kFailed) {
          s.lifecycle_state = SessionLifecycleState::kStopped;
        }
      });
    }

    // IsFinished only after WorkerState/Domain/storage are gone.
    finished_.store(true, std::memory_order_release);
    if (notify_ui_) {
      notify_ui_();
    }
  };

  try {
    apptraverse::EnsureObjectRegistration();
    EnsureChatDemoModelRegistration();
    EnsureAetherLinkRegistration();

    auto const model_dir = config.state_dir / "model";
    auto const aether_dir = config.state_dir / "aether";
    std::filesystem::create_directories(model_dir);
    std::filesystem::create_directories(aether_dir);

    worker = std::make_unique<WorkerState>(model_dir);
    auto& storage = worker->storage;
    auto& domain = worker->domain;
    auto& workspace = worker->workspace;
    auto& aether_runtime = worker->endpoint;
    auto& transport = worker->transport;
    auto& sync_runtime = worker->sync_runtime;
    auto& my_uid = worker->my_uid;
    auto& aether_ready = worker->aether_ready;
    auto& identity_conflict = worker->identity_conflict;
    auto& remote_presence_map = worker->remote_presence_map;
    auto& endpoint_by_pending_entry = worker->endpoint_by_pending_entry;
    auto& waiting_entry_by_endpoint = worker->waiting_entry_by_endpoint;
    auto& runtime = worker->runtime;
    auto& publication_dirty = worker->publication_dirty;
    auto& processed_edit_revisions = worker->processed_edit_revisions;
    auto& pending_select_ack = worker->pending_select_ack;

    if (!storage.Enumerate(kLocalWorkspaceRootId).empty()) {
      workspace = ChatWorkspace::ptr::Declare(
          ae::CreateWith{domain}.with_id(kLocalWorkspaceRootId));
      try {
        workspace.Load();
      } catch (std::exception const& ex) {
        throw std::runtime_error(
            std::string(ex.what()) +
            " Choose another --state-dir; the existing directory was not "
            "modified.");
      }
      if (!workspace) {
        throw std::runtime_error("Failed to load existing workspace root");
      }
    } else {
      workspace = ChatWorkspace::ptr::Create(
          ae::CreateWith{domain}.with_id(kLocalWorkspaceRootId));
      InitializeRuntimeNode(*workspace);
      workspace.Save();
    }

  auto const persist_workspace = [&workspace]() {
    if (workspace.is_valid()) {
      workspace.Save();
    }
  };

    EnsureChatDemoRuntimeRegistration();
    runtime = ChatDemoRuntimeState::ptr::Create(
        ae::CreateWith{worker->runtime_domain});
    InitializeRuntimeNode(*runtime);
    if (!ConfigureDemoRole(*workspace, config.role, persist_workspace)) {
      throw std::runtime_error(
          "This profile is already configured for a different Host/Client "
          "role. Choose another --state-dir; the existing directory was not "
          "modified.");
    }
    if (config.host_uid_prefill.has_value() &&
        workspace->demo_role == DemoRole::kClient) {
      apptraverse::example::chat_demo::SetHostUidInput(*workspace, *config.host_uid_prefill, persist_workspace);
    }
    UpdateStatus([&workspace](ChatRuntimeStatus& s) {
      s.demo_role = workspace->demo_role;
      s.host_uid_input = workspace->host_uid_input;
    });

  // Ensure every chat room blocks journal compaction for demo retry/dedup
  for (auto const& entry : workspace->chats) {
    if (entry.is_valid() && entry->room.is_valid()) {
      entry->room->SetJournalCompactionBlocked(true);
    }
  }

  auto const publish_now_unlocked =
      [this, &workspace, &notify_ui, &processed_edit_revisions,
       &pending_select_ack](bool is_initial) {
        assert(!channel_.is_publication_busy() &&
               "publish_now_unlocked requires a free publication slot");
        auto* buf = channel_.AcquireProducer();
        if (is_initial) {
          SerializeInitialPublication(*workspace, buf->sink);
        } else {
          SerializeStructuralNodePublication(*workspace, buf->sink);
        }
        channel_.NotePublished();
        pending_publication_metadata_ = PendingPublicationMetadata{
            .kind = is_initial ? ChatPublicationKind::kInitial
                               : ChatPublicationKind::kStructural,
            .serial = next_publication_serial_++,
            .edit_revisions_by_entry = processed_edit_revisions,
            .selected_chat_ack = pending_select_ack,
        };
        pending_select_ack.reset();
        channel_.PublishProducer();
        if (notify_ui) {
          notify_ui();
        }
      };

  auto const publish_now = [this, &workspace, &publication_dirty,
                            publish_now_unlocked](bool is_initial) {
    if (!workspace.is_valid()) {
      return;
    }
    std::lock_guard<std::mutex> pub_lock{publication_mu_};
    if (channel_.is_publication_busy()) {
      publication_dirty = true;
      return;
    }
    publish_now_unlocked(is_initial);
    publication_dirty = false;
  };

  // 5. Publish local workspace immediately before network registration
  publish_now(/*is_initial=*/true);

  worker->network_cfg = IAetherFrameEndpoint::Config{
      .state_dir = aether_dir,
      .client_name = config.aether_client_name,
      .heartbeat_period_ms = 1000,
      .offline_after_ms = 4000,
  };
  worker->network_epoch = 1;

  // Runtime presence helper
    auto const set_peer_presence =
        [this, &remote_presence_map](std::string const& peer, PeerPresence p) {
          AssertModelThread();
          remote_presence_map[peer] = p;
          UpdateStatus([&remote_presence_map](ChatRuntimeStatus& s) {
            s.remote_presence = remote_presence_map;
          });
        };

    auto const model_dispatcher = [this, &publication_dirty](ModelTask task) {
      EnqueueInternalModelWork([t = std::move(task), &publication_dirty]() mutable {
        if (t) {
          t();
          publication_dirty = true;
        }
      });
    };

    constexpr auto kJoinRetryInterval = std::chrono::seconds(1);
    constexpr auto kJoinAttemptDeadline = std::chrono::seconds(30);

    auto const clear_join_scheduler = [&worker]() {
      worker->join_sched_attempt_id = {};
      worker->join_frozen_bytes.clear();
      worker->join_attempt_start = {};
      worker->join_last_send.reset();
    };

    auto const project_demo_status =
        [this, &workspace, &runtime, &worker](bool force = false) {
          AssertModelThread();
          if (!workspace.is_valid() || !runtime.is_valid()) {
            return;
          }
          auto const runtime_gen = runtime->Generation();
          auto const workspace_gen = workspace->Generation();
          auto const phase = runtime->join_phase;
          auto const err = runtime->last_error;
          auto const host_in = workspace->host_uid_input;
          auto const local_uid = workspace->local_endpoint_uid;
          if (!force &&
              runtime_gen == worker->last_projected_runtime_gen &&
              workspace_gen == worker->last_projected_workspace_gen &&
              phase == worker->last_projected_join_phase &&
              err == worker->last_projected_join_error &&
              host_in == worker->last_projected_host_input &&
              local_uid == worker->last_projected_local_uid) {
            return;
          }
          worker->last_projected_runtime_gen = runtime_gen;
          worker->last_projected_workspace_gen = workspace_gen;
          worker->last_projected_join_phase = phase;
          worker->last_projected_join_error = err;
          worker->last_projected_host_input = host_in;
          worker->last_projected_local_uid = local_uid;

          UpdateStatus([&](ChatRuntimeStatus& s) {
            s.demo_role = workspace->demo_role;
            s.host_uid_input = host_in;
            s.local_endpoint_uid = local_uid;
            s.join_phase = phase;
            if (workspace->demo_role == DemoRole::kHost && local_uid.empty()) {
              s.join_status_text = "Registering…";
            } else if (workspace->demo_role == DemoRole::kClient) {
              switch (phase) {
                case ChatJoinPhase::kJoining:
                  s.join_status_text = "Joining…";
                  break;
                case ChatJoinPhase::kAccepted:
                  // Accepted is not Joined; room import may still be pending.
                  s.join_status_text = "Syncing…";
                  break;
                case ChatJoinPhase::kJoined:
                  s.join_status_text = "Joined";
                  break;
                case ChatJoinPhase::kFailed:
                  s.join_status_text = err;
                  s.error_text = err;
                  break;
                default:
                  s.join_status_text.clear();
                  break;
              }
              if (phase != ChatJoinPhase::kFailed && err.empty()) {
                if (s.error_text.find("Join") != std::string::npos ||
                    s.error_text.find("join") != std::string::npos ||
                    s.error_text == "Invalid Host UID" ||
                    s.error_text.find("timed out") != std::string::npos) {
                  s.error_text.clear();
                }
              }
            } else {
              s.join_status_text.clear();
            }
          });
        };

    auto const arm_join_scheduler =
        [&worker, &runtime, &workspace, &clear_join_scheduler]() {
          if (!runtime.is_valid() ||
              workspace->demo_role != DemoRole::kClient) {
            clear_join_scheduler();
            return;
          }
          if (runtime->join_phase != ChatJoinPhase::kJoining &&
              runtime->join_phase != ChatJoinPhase::kAccepted) {
            clear_join_scheduler();
            return;
          }
          if (!runtime->join_attempt_id.is_valid() ||
              runtime->expected_host_uid.empty()) {
            clear_join_scheduler();
            return;
          }
          if (worker->join_sched_attempt_id == runtime->join_attempt_id &&
              !worker->join_frozen_bytes.empty()) {
            return;
          }
          ChatBootstrapMessage req;
          req.kind = ChatBootstrapKind::kJoinRequest;
          req.attempt_id = runtime->join_attempt_id;
          if (auto entry = FindEntryById(*workspace, runtime->join_entry_id);
              entry.is_valid() && entry->room.is_valid()) {
            req.room_id = entry->room.id();
          }
          std::vector<std::uint8_t> bytes;
          if (!EncodeChatBootstrap(req, bytes)) {
            clear_join_scheduler();
            return;
          }
          worker->join_sched_attempt_id = runtime->join_attempt_id;
          worker->join_frozen_bytes = std::move(bytes);
          worker->join_attempt_start = std::chrono::steady_clock::now();
          worker->join_last_send.reset();
        };

    auto const drive_join_send =
        [this, &worker, &runtime, &workspace, &aether_runtime, &aether_ready,
         &my_uid, &sync_runtime, &waiting_entry_by_endpoint,
         &clear_join_scheduler](std::chrono::steady_clock::time_point now) {
          AssertModelThread();
          if (!runtime.is_valid() || !aether_runtime ||
              workspace->demo_role != DemoRole::kClient) {
            return;
          }
          if (runtime->join_phase != ChatJoinPhase::kJoining &&
              runtime->join_phase != ChatJoinPhase::kAccepted) {
            clear_join_scheduler();
            return;
          }
          if (!worker->join_sched_attempt_id.is_valid() ||
              worker->join_frozen_bytes.empty() ||
              worker->join_sched_attempt_id != runtime->join_attempt_id) {
            return;
          }
          if (!aether_ready || my_uid.empty()) {
            return;
          }
          // One attempt budget covers Joining and Accepted until the room
          // is durably bound. Duplicate Accept does not restart the clock.
          if (now - worker->join_attempt_start >= kJoinAttemptDeadline) {
            auto fail = JoinHostFailedEvent::ptr::Create(
                ae::CreateWith{*runtime->domain});
            fail->attempt_id = runtime->join_attempt_id;
            fail->reason = runtime->join_phase == ChatJoinPhase::kAccepted
                               ? "Room sync timed out"
                               : "Host response timed out";
            if (runtime->CanApply(*fail)) {
              runtime->Commit(fail);
            }
            if (sync_runtime) {
              sync_runtime->ForgetInitialNodeFromEndpoint(
                  runtime->expected_host_uid);
            }
            waiting_entry_by_endpoint.erase(runtime->expected_host_uid);
            clear_join_scheduler();
            return;
          }
          if (worker->join_last_send.has_value() &&
              now - *worker->join_last_send < kJoinRetryInterval) {
            return;
          }
          aether_runtime->OpenPeer(runtime->expected_host_uid);
          aether_runtime->SendControl(runtime->expected_host_uid,
                                      worker->join_frozen_bytes);
          worker->join_last_send = now;
        };

    auto const send_join_rejected =
        [&aether_runtime](std::string const& client_uid, ae::ObjId attempt_id,
                          std::string reason) {
          if (!aether_runtime || client_uid.empty()) {
            return;
          }
          ChatBootstrapMessage rejected;
          rejected.kind = ChatBootstrapKind::kJoinRejected;
          rejected.attempt_id = attempt_id;
          rejected.rejection_reason = std::move(reason);
          std::vector<std::uint8_t> bytes;
          if (!EncodeChatBootstrap(rejected, bytes)) {
            return;
          }
          aether_runtime->OpenPeer(client_uid);
          aether_runtime->SendControl(client_uid, std::move(bytes));
        };

    auto const host_accept_client =
        [&workspace, &persist_workspace, &sync_runtime, &aether_runtime, &my_uid,
         &publication_dirty, &aether_ready, &send_join_rejected](
            std::string const& client_uid, ChatBootstrapMessage const& req) {
          if (workspace->demo_role != DemoRole::kHost) {
            return;
          }
          if (!aether_ready || my_uid.empty() || !sync_runtime) {
            // Pre-readiness: ignore without mutation; Client retries.
            return;
          }
          if (client_uid.empty() || client_uid == my_uid) {
            return;
          }
          ChatEntry::ptr existing;
          for (auto const& entry : workspace->chats) {
            if (entry.is_valid() && entry->peer_uid == client_uid) {
              existing = entry;
              break;
            }
          }
          if (existing.is_valid() && existing->room.is_valid()) {
            if (req.room_id.is_valid() && req.room_id != existing->room.id()) {
              send_join_rejected(
                  client_uid, req.attempt_id,
                  "Join conflict: saved room does not match host room");
              return;
            }
            persist_workspace();
            ChatBootstrapMessage accepted;
            accepted.kind = ChatBootstrapKind::kJoinAccepted;
            accepted.attempt_id = req.attempt_id;
            accepted.room_id = existing->room.id();
            std::vector<std::uint8_t> bytes;
            if (EncodeChatBootstrap(accepted, bytes) && aether_runtime) {
              aether_runtime->OpenPeer(client_uid);
              aether_runtime->SendControl(client_uid, std::move(bytes));
            }
            // Acceptance first, then snapshot (same order as new-room path).
            auto existing_reg = sync_runtime->FindNode(existing->room.id());
            if (!existing_reg.is_valid()) {
              sync_runtime->RegisterNode(existing->room);
            }
            ae::ObjId remote_share_id;
            for (auto const& share : existing->room->shares) {
              if (share.link.is_valid() &&
                  share.link->EndpointUid() == client_uid) {
                remote_share_id = share.share_id;
                break;
              }
            }
            if (remote_share_id.is_valid()) {
              auto const idx =
                  existing->room->FindLinkSyncIndexForShare(remote_share_id);
              if (idx < existing->room->link_sync_states.size()) {
                auto state = existing->room->link_sync_states[idx];
                if (state.is_valid()) {
                  if (!state.is_loaded()) {
                    state.Load();
                  }
                  if (state->GetInitialSyncPhase() !=
                      apptraverse::InitialSyncPhase::Complete) {
                    sync_runtime->SyncInitialState(existing->room.id(),
                                                   remote_share_id);
                  }
                }
              }
            }
            publication_dirty = true;
            return;
          }

          auto entry =
              OpenOrSelectChat(*workspace, client_uid, persist_workspace);
          if (!entry.is_valid()) {
            return;
          }
          auto room = ChatRoom::ptr::Create(ae::CreateWith{*workspace->domain});
          InitializeRuntimeNode(*room);
          room->SetJournalCompactionBlocked(true);
          auto local_link =
              AetherLink::ptr::Create(ae::CreateWith{*workspace->domain});
          local_link->endpoint_uid = my_uid;
          InitializeRuntimeNode(*local_link);
          auto remote_link =
              AetherLink::ptr::Create(ae::CreateWith{*workspace->domain});
          remote_link->endpoint_uid = client_uid;
          InitializeRuntimeNode(*remote_link);
          room->AddShare(local_link, apptraverse::ShareAccess::ReadWrite);
          room->AddShare(remote_link, apptraverse::ShareAccess::ReadWrite);
          if (!BindChat(*entry, remote_link, room, persist_workspace)) {
            return;
          }
          persist_workspace();
          ChatBootstrapMessage accepted;
          accepted.kind = ChatBootstrapKind::kJoinAccepted;
          accepted.attempt_id = req.attempt_id;
          accepted.room_id = room.id();
          std::vector<std::uint8_t> bytes;
          if (EncodeChatBootstrap(accepted, bytes) && aether_runtime) {
            aether_runtime->OpenPeer(client_uid);
            aether_runtime->SendControl(client_uid, std::move(bytes));
          }
          if (!sync_runtime->FindNode(room.id()).is_valid()) {
            sync_runtime->RegisterNode(room);
          }
          ae::ObjId remote_share_id;
          for (auto const& share : room->shares) {
            if (share.link.is_valid() &&
                share.link->EndpointUid() == client_uid) {
              remote_share_id = share.share_id;
              break;
            }
          }
          if (remote_share_id.is_valid()) {
            sync_runtime->SyncInitialState(room.id(), remote_share_id);
          }
          publication_dirty = true;
        };

    auto const handle_control =
        [this, &workspace, &runtime, &publication_dirty,
         &waiting_entry_by_endpoint, &host_accept_client, &identity_conflict,
         &sync_runtime, &clear_join_scheduler](
            std::string source, std::vector<std::uint8_t> bytes) {
          AssertModelThread();
          if (identity_conflict || !runtime.is_valid()) {
            return;
          }
          ChatBootstrapMessage msg;
          if (!DecodeChatBootstrap(bytes, msg)) {
            return;
          }
          if (workspace->demo_role == DemoRole::kHost &&
              msg.kind == ChatBootstrapKind::kJoinRequest) {
            host_accept_client(source, msg);
            publication_dirty = true;
            return;
          }
          if (workspace->demo_role != DemoRole::kClient) {
            return;
          }
          if (msg.kind == ChatBootstrapKind::kJoinRejected) {
            std::string canonical_source = source;
            (void)TryCanonicalizeAetherUid(source, canonical_source);
            if (canonical_source != runtime->expected_host_uid ||
                msg.attempt_id != runtime->join_attempt_id) {
              return;
            }
            auto fail = JoinHostFailedEvent::ptr::Create(
                ae::CreateWith{*runtime->domain});
            fail->attempt_id = msg.attempt_id;
            fail->reason = msg.rejection_reason.empty()
                               ? std::string{"Join rejected"}
                               : msg.rejection_reason;
            if (!runtime->CanApply(*fail)) {
              return;
            }
            runtime->Commit(fail);
            if (sync_runtime) {
              sync_runtime->ForgetInitialNodeFromEndpoint(source);
            }
            waiting_entry_by_endpoint.erase(source);
            clear_join_scheduler();
            publication_dirty = true;
            return;
          }
          if (msg.kind != ChatBootstrapKind::kJoinAccepted) {
            return;
          }
          // Verify before any Event or waiting-map side effect.
          {
            std::string canonical_source = source;
            (void)TryCanonicalizeAetherUid(source, canonical_source);
            if (canonical_source != runtime->expected_host_uid ||
                msg.attempt_id != runtime->join_attempt_id ||
                !msg.room_id.is_valid()) {
              return;
            }
          }
          if (runtime->join_phase == ChatJoinPhase::kJoined &&
              runtime->bound_room_id == msg.room_id &&
              runtime->join_attempt_id == msg.attempt_id) {
            return;
          }
          if (runtime->join_phase == ChatJoinPhase::kAccepted &&
              runtime->accepted_room_id == msg.room_id &&
              runtime->join_attempt_id == msg.attempt_id) {
            if (sync_runtime) {
              sync_runtime->ExpectInitialNodeFromEndpoint(
                  source, ChatRoom::kClassId, msg.room_id);
            }
            waiting_entry_by_endpoint[source] = runtime->join_entry_id;
            return;
          }
          if (runtime->join_phase == ChatJoinPhase::kAccepted &&
              runtime->accepted_room_id.is_valid() &&
              runtime->accepted_room_id != msg.room_id) {
            auto fail = JoinHostFailedEvent::ptr::Create(
                ae::CreateWith{*runtime->domain});
            fail->attempt_id = msg.attempt_id;
            fail->reason = "Join conflict: host offered a different room";
            if (runtime->CanApply(*fail)) {
              runtime->Commit(fail);
              clear_join_scheduler();
              publication_dirty = true;
            }
            return;
          }
          if (runtime->join_phase != ChatJoinPhase::kJoining) {
            return;
          }
          auto ev = JoinHostAcceptedEvent::ptr::Create(
              ae::CreateWith{*runtime->domain});
          ev->attempt_id = msg.attempt_id;
          ev->source_uid = source;
          if (std::string canonical_source = source;
              TryCanonicalizeAetherUid(source, canonical_source)) {
            ev->source_uid = canonical_source;
          }
          ev->room_id = msg.room_id;
          if (!runtime->CanApply(*ev)) {
            return;
          }
          runtime->Commit(ev);
          auto entry = FindEntryById(*workspace, runtime->join_entry_id);
          if (entry.is_valid() && entry->room.is_valid() &&
              entry->room.id() == msg.room_id) {
            auto done = JoinHostCompletedEvent::ptr::Create(
                ae::CreateWith{*runtime->domain});
            done->attempt_id = msg.attempt_id;
            done->room_id = msg.room_id;
            if (runtime->CanApply(*done)) {
              runtime->Commit(done);
              clear_join_scheduler();
            }
          } else {
            waiting_entry_by_endpoint[source] = runtime->join_entry_id;
            if (sync_runtime) {
              sync_runtime->ExpectInitialNodeFromEndpoint(
                  source, ChatRoom::kClassId, msg.room_id);
            }
          }
          publication_dirty = true;
        };

    auto const begin_join =
        [this, &workspace, &runtime, &persist_workspace, &sync_runtime, &my_uid,
         &publication_dirty, &waiting_entry_by_endpoint, &arm_join_scheduler,
         &clear_join_scheduler]() {
          AssertModelThread();
          if (workspace->demo_role != DemoRole::kClient || !runtime.is_valid()) {
            return;
          }
          apptraverse::example::chat_demo::SetHostUidInput(
              *workspace, workspace->host_uid_input, persist_workspace);
          std::string canonical;
          if (!TryCanonicalizeAetherUid(workspace->host_uid_input, canonical) ||
              (!my_uid.empty() && canonical == my_uid)) {
            auto fail = JoinHostFailedEvent::ptr::Create(
                ae::CreateWith{*runtime->domain});
            fail->attempt_id = runtime->join_attempt_id;
            fail->reason = "Invalid Host UID";
            if (runtime->CanApply(*fail)) {
              runtime->Commit(fail);
            }
            clear_join_scheduler();
            publication_dirty = true;
            return;
          }
          auto entry =
              OpenOrSelectChat(*workspace, canonical, persist_workspace);
          if (!entry.is_valid()) {
            return;
          }

          // Same host while Joining/Accepted: reuse attempt; no new Event.
          if ((runtime->join_phase == ChatJoinPhase::kJoining ||
               runtime->join_phase == ChatJoinPhase::kAccepted) &&
              runtime->expected_host_uid == canonical) {
            arm_join_scheduler();
            return;
          }

          // Switch host: supersede old attempt expectation.
          if ((runtime->join_phase == ChatJoinPhase::kJoining ||
               runtime->join_phase == ChatJoinPhase::kAccepted) &&
              !runtime->expected_host_uid.empty() &&
              runtime->expected_host_uid != canonical) {
            if (sync_runtime) {
              sync_runtime->ForgetInitialNodeFromEndpoint(
                  runtime->expected_host_uid);
            }
            waiting_entry_by_endpoint.erase(runtime->expected_host_uid);
            clear_join_scheduler();
          }

          auto req = JoinHostRequestedEvent::ptr::Create(
              ae::CreateWith{*runtime->domain});
          req->entry_id = entry.id();
          req->host_uid = canonical;
          if (!runtime->CanApply(*req)) {
            return;
          }
          runtime->Commit(req);
          // Snapshot expectation is registered only after Host acceptance.
          arm_join_scheduler();
          publication_dirty = true;
        };

    worker->handle_control = handle_control;

    on_set_host_uid_input_ = [&workspace, &persist_workspace, &publication_dirty,
                               &project_demo_status](std::string text) {
      if (apptraverse::example::chat_demo::SetHostUidInput(*workspace, text, persist_workspace)) {
        publication_dirty = true;
        project_demo_status();
      }
    };

    on_join_host_ = [&begin_join]() { begin_join(); };

    on_copy_host_uid_ = [this, &workspace, &runtime, &publication_dirty]() {
      if (workspace->demo_role != DemoRole::kHost ||
          workspace->local_endpoint_uid.empty() || !runtime.is_valid()) {
        return;
      }
      auto ev = CopyHostUidRequestedEvent::ptr::Create(
          ae::CreateWith{*runtime->domain});
      ev->uid = workspace->local_endpoint_uid;
      runtime->Commit(ev);
      UpdateStatus([&runtime](ChatRuntimeStatus& s) {
        s.pending_copy = CopyHostUidEffect{
            .request_id = runtime->copy_request_id,
            .uid_text = runtime->copy_uid,
        };
      });
      publication_dirty = true;
    };

  on_select_chat_ = [&workspace, &persist_workspace, &publication_dirty,
                     &pending_select_ack](ae::ObjId entry_id) {
    if (apptraverse::example::chat_demo::SelectChat(*workspace, entry_id,
                                                    persist_workspace)) {
      pending_select_ack = entry_id;
      publication_dirty = true;
    }
  };

  on_edit_draft_ = [this, &workspace, &persist_workspace, &publication_dirty,
                    &processed_edit_revisions](ae::ObjId entry_id, std::string text,
                                             std::uint64_t edit_revision) {
    AssertModelThread();
    auto const reject_edit = [this, entry_id, edit_revision](std::string reason) {
      UpdateStatus([entry_id, edit_revision, reason = std::move(reason)](
                       ChatRuntimeStatus& s) {
        s.error_text = reason;
        s.latest_edit_result_by_entry[entry_id] = DraftCommandResult{
            .entry_id = entry_id,
            .kind = DraftCommandKind::kEdit,
            .revision = edit_revision,
            .outcome = DraftCommandOutcome::kRejected,
            .failure_reason = reason,
        };
      });
    };
    if (!FieldWithinUserCommandLimit(text, kMaxDraftTextBytes)) {
      reject_edit("Draft too large");
      publication_dirty = true;
      return;
    }
    for (auto const& entry : workspace->chats) {
      if (entry.is_valid() && entry.id() == entry_id) {
        bool const unchanged = entry->draft == text;
        if (SetDraft(*entry, text, persist_workspace)) {
          processed_edit_revisions[entry_id] = edit_revision;
          if (!unchanged) {
            publication_dirty = true;
          }
          UpdateStatus([entry_id, edit_revision](ChatRuntimeStatus& s) {
            s.latest_edit_result_by_entry[entry_id] = DraftCommandResult{
                .entry_id = entry_id,
                .kind = DraftCommandKind::kEdit,
                .revision = edit_revision,
                .outcome = DraftCommandOutcome::kAccepted,
            };
          });
        }
        return;
      }
    }
    reject_edit("Unknown chat entry");
  };

  on_send_draft_ = [this, &workspace, &persist_workspace, &publication_dirty,
                    &processed_edit_revisions](ae::ObjId entry_id,
                                               std::string current_text,
                                               std::uint64_t edit_revision) {
    AssertModelThread();
    auto const reject_send = [this, entry_id, edit_revision](std::string reason) {
      UpdateStatus([entry_id, edit_revision, reason = std::move(reason)](
                       ChatRuntimeStatus& s) {
        s.error_text = reason;
        s.latest_send_result_by_entry[entry_id] = DraftCommandResult{
            .entry_id = entry_id,
            .kind = DraftCommandKind::kSend,
            .revision = edit_revision,
            .outcome = DraftCommandOutcome::kRejected,
            .failure_reason = reason,
        };
      });
    };
    if (!FieldWithinUserCommandLimit(current_text, kMaxDraftTextBytes)) {
      reject_send("Draft too large");
      publication_dirty = true;
      return;
    }
    for (auto const& entry : workspace->chats) {
      if (entry.is_valid() && entry.id() == entry_id) {
        SetDraft(*entry, current_text, persist_workspace);
        auto now_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        auto msg_id = SubmitDraft(*workspace, *entry, now_us, persist_workspace);
        if (!msg_id.origin_uid.empty() && msg_id.origin_sequence > 0) {
          processed_edit_revisions[entry_id] = edit_revision;
          publication_dirty = true;
          UpdateStatus([entry_id, edit_revision, msg_id](ChatRuntimeStatus& s) {
            s.latest_send_result_by_entry[entry_id] = DraftCommandResult{
                .entry_id = entry_id,
                .kind = DraftCommandKind::kSend,
                .revision = edit_revision,
                .outcome = DraftCommandOutcome::kAccepted,
                .accepted_send_id = msg_id,
            };
          });
        } else {
          reject_send("Send rejected");
          publication_dirty = true;
        }
        return;
      }
    }
    reject_send("Unknown chat entry");
  };

  on_save_scroll_ = [&workspace, &persist_workspace, &publication_dirty](
                        ae::ObjId entry_id, ScrollAnchor anchor) {
    for (auto const& entry : workspace->chats) {
      if (entry.is_valid() && entry.id() == entry_id) {
        if (SetScroll(*entry, anchor, persist_workspace)) {
          publication_dirty = true;
        }
        break;
      }
    }
  };

  on_save_bounds_ = [&workspace, &persist_workspace, &publication_dirty](DesktopBounds bounds) {
    if (SetDesktopBounds(*workspace, bounds, persist_workspace)) {
      publication_dirty = true;
    }
  };

  on_checkpoint_ = [this, &persist_workspace](std::uint64_t request_id) {
    // Root Save only — no geometry/message mutation.
    try {
      persist_workspace();
      UpdateStatus([request_id](ChatRuntimeStatus& s) {
        s.completed_checkpoint_id = request_id;
      });
    } catch (std::exception const& ex) {
      UpdateStatus([&ex](ChatRuntimeStatus& s) {
        s.error_text = std::string("Checkpoint Save failed: ") + ex.what();
      });
    }
  };

  std::function<void()> start_network;
  start_network = [this, &worker, &aether_runtime, &workspace, &persist_workspace,
                   &my_uid, &publication_dirty, &identity_conflict, &aether_ready,
                   &transport, &sync_runtime, &domain, &storage, &model_dispatcher,
                   
                   &endpoint_by_pending_entry, &waiting_entry_by_endpoint,
                   &remote_presence_map, set_peer_presence]() {
    aether_runtime = endpoint_factory_();
    assert(aether_runtime && "EndpointFactory must create one endpoint");
    std::uint64_t const epoch = worker->network_epoch;
    auto cfg = worker->network_cfg;
    aether_runtime->Start(
      std::move(cfg),
      /*on_uid=*/
      [this, &worker, &workspace, &persist_workspace, &my_uid, &publication_dirty,
       &identity_conflict, &aether_runtime, epoch](std::string uid) {
        EnqueueInternalModelWork([this, &worker, &workspace, &persist_workspace, &my_uid,
                                  &publication_dirty, &identity_conflict,
                                  &aether_runtime, epoch, uid = std::move(uid)]() mutable {
          AssertModelThread();
          if (!worker || worker->network_epoch != epoch) {
            return;
          }
          my_uid = uid;

          if (!workspace->local_endpoint_uid.empty() &&
              workspace->local_endpoint_uid != my_uid) {
            identity_conflict = true;
            UpdateStatus([&my_uid, &workspace](ChatRuntimeStatus& s) {
              s.lifecycle_state = SessionLifecycleState::kFailed;
              s.error_text =
                  "Local endpoint UID conflict: persisted " +
                  workspace->local_endpoint_uid + " vs Aether " + my_uid;
            });
            publication_dirty = true;
            aether_runtime->RequestStop();
            return;
          }

          if (!BindLocalEndpoint(*workspace, my_uid, persist_workspace)) {
            identity_conflict = true;
            UpdateStatus([&my_uid, &workspace](ChatRuntimeStatus& s) {
              s.lifecycle_state = SessionLifecycleState::kFailed;
              s.error_text =
                  "Local endpoint UID conflict: persisted " +
                  workspace->local_endpoint_uid + " vs Aether " + my_uid;
            });
            publication_dirty = true;
            aether_runtime->RequestStop();
            return;
          }

          UpdateStatus([&my_uid](ChatRuntimeStatus& s) {
            s.local_endpoint_uid = my_uid;
          });
          publication_dirty = true;
        });
      },
      /*on_ready=*/
      [this, &worker, &aether_ready, &workspace, &aether_runtime, &transport, &sync_runtime,
       &domain, &storage, &my_uid, &model_dispatcher, &persist_workspace,
        &publication_dirty,
       &endpoint_by_pending_entry, &waiting_entry_by_endpoint,
       &identity_conflict, epoch]() {
        EnqueueInternalModelWork([this, &worker, &aether_ready, &workspace, &aether_runtime,
                                  &transport, &sync_runtime, &domain, &storage,
                                  &my_uid, &model_dispatcher, &persist_workspace,
                                  &publication_dirty,
                                  &endpoint_by_pending_entry, &waiting_entry_by_endpoint,
                                  &identity_conflict, epoch]() {
          AssertModelThread();
          if (!worker || worker->network_epoch != epoch || identity_conflict) {
            return;
          }

          aether_ready = true;
          UpdateStatus([](ChatRuntimeStatus& s) {
            s.lifecycle_state = SessionLifecycleState::kReady;
          });

          transport = std::make_unique<AetherByteTransport>(
              *aether_runtime, my_uid, model_dispatcher);
          sync_runtime = std::make_unique<SharedSyncRuntime>(domain, storage, *transport);

          sync_runtime->AllowStandaloneEventClass(MessageAddedEvent::kClassId);

          sync_runtime->SetInitialNodeImportedCallback(
              [&workspace, &persist_workspace, &publication_dirty,
               &waiting_entry_by_endpoint, &endpoint_by_pending_entry, &worker](
                  std::string const& source_endpoint,
                  SharedNode::ptr imported_node) -> bool {
                if (!imported_node.is_valid()) {
                  return false;
                }
                auto& runtime = worker->runtime;
                if (!runtime.is_valid() ||
                    runtime->join_phase != ChatJoinPhase::kAccepted) {
                  return false;
                }
                if (source_endpoint != runtime->expected_host_uid) {
                  return false;
                }
                if (ae::Registry::GetRegistry().GenerationDistance(
                        ChatRoom::kClassId, imported_node->GetClassId()) < 0) {
                  return false;
                }
                auto room = ChatRoom::ptr::MakeFromThis(
                    static_cast<ChatRoom*>(&*imported_node));
                if (!room.is_valid()) {
                  return false;
                }
                if (!runtime->accepted_room_id.is_valid() ||
                    room.id() != runtime->accepted_room_id) {
                  return false;
                }
                room->SetJournalCompactionBlocked(true);

                auto waiting_it =
                    waiting_entry_by_endpoint.find(source_endpoint);
                if (waiting_it == waiting_entry_by_endpoint.end()) {
                  return false;
                }
                if (waiting_it->second != runtime->join_entry_id) {
                  return false;
                }
                auto waiting_entry =
                    FindEntryById(*workspace, waiting_it->second);
                if (!waiting_entry.is_valid()) {
                  return false;
                }
                if (waiting_entry->room.is_valid() &&
                    waiting_entry->room.id() != room.id()) {
                  return false;
                }

                apptraverse::Link::ptr remote_link;
                for (auto const& share : room->shares) {
                  if (share.link.is_valid() &&
                      share.link->EndpointUid() == source_endpoint) {
                    remote_link = share.link;
                    break;
                  }
                }
                if (!remote_link.is_valid()) {
                  return false;
                }

                if (waiting_entry->room.is_valid() &&
                    waiting_entry->room.id() == room.id()) {
                  auto done = JoinHostCompletedEvent::ptr::Create(
                      ae::CreateWith{*runtime->domain});
                  done->attempt_id = runtime->join_attempt_id;
                  done->room_id = room.id();
                  if (runtime->CanApply(*done) &&
                      runtime->join_phase == ChatJoinPhase::kAccepted) {
                    runtime->Commit(done);
                  }
                  endpoint_by_pending_entry.erase(waiting_entry.id());
                  waiting_entry_by_endpoint.erase(source_endpoint);
                  worker->join_sched_attempt_id = {};
                  worker->join_frozen_bytes.clear();
                  worker->join_last_send.reset();
                  publication_dirty = true;
                  return true;
                }

                if (!BindChat(*waiting_entry, remote_link, room,
                              persist_workspace)) {
                  return false;
                }
                persist_workspace();
                endpoint_by_pending_entry.erase(waiting_entry.id());
                waiting_entry_by_endpoint.erase(source_endpoint);
                if (runtime->join_entry_id == waiting_entry.id()) {
                  auto done = JoinHostCompletedEvent::ptr::Create(
                      ae::CreateWith{*runtime->domain});
                  done->attempt_id = runtime->join_attempt_id;
                  done->room_id = room.id();
                  if (runtime->CanApply(*done)) {
                    runtime->Commit(done);
                  }
                }
                worker->join_sched_attempt_id = {};
                worker->join_frozen_bytes.clear();
                worker->join_last_send.reset();
                publication_dirty = true;
                return true;
              });

          for (auto const& entry : workspace->chats) {
            if (entry.is_valid() && entry->room.is_valid()) {
              if (!sync_runtime->FindNode(entry->room.id()).is_valid()) {
                sync_runtime->RegisterNode(entry->room);
              }
            }
          }

          if (worker->runtime.is_valid() &&
              worker->runtime->join_phase == ChatJoinPhase::kAccepted &&
              worker->runtime->accepted_room_id.is_valid() &&
              !worker->runtime->expected_host_uid.empty()) {
            waiting_entry_by_endpoint[worker->runtime->expected_host_uid] =
                worker->runtime->join_entry_id;
            sync_runtime->ExpectInitialNodeFromEndpoint(
                worker->runtime->expected_host_uid, ChatRoom::kClassId,
                worker->runtime->accepted_room_id);
          }

          for (auto const& entry : workspace->chats) {
            if (entry.is_valid() && entry->peer_link.is_valid()) {
              std::string const& uid = entry->peer_link->EndpointUid();
              if (!uid.empty()) {
                aether_runtime->OpenPeer(uid);
              }
            }
          }

          if (workspace->demo_role == DemoRole::kClient &&
              worker->runtime.is_valid() &&
              (worker->runtime->join_phase == ChatJoinPhase::kJoining ||
               worker->runtime->join_phase == ChatJoinPhase::kAccepted)) {
            if (worker->join_sched_attempt_id !=
                    worker->runtime->join_attempt_id ||
                worker->join_frozen_bytes.empty()) {
              ChatBootstrapMessage req;
              req.kind = ChatBootstrapKind::kJoinRequest;
              req.attempt_id = worker->runtime->join_attempt_id;
              if (auto entry = FindEntryById(
                      *workspace, worker->runtime->join_entry_id);
                  entry.is_valid() && entry->room.is_valid()) {
                req.room_id = entry->room.id();
              }
              std::vector<std::uint8_t> bytes;
              if (EncodeChatBootstrap(req, bytes)) {
                worker->join_sched_attempt_id =
                    worker->runtime->join_attempt_id;
                worker->join_frozen_bytes = std::move(bytes);
                if (worker->join_attempt_start ==
                    std::chrono::steady_clock::time_point{}) {
                  worker->join_attempt_start =
                      std::chrono::steady_clock::now();
                }
                worker->join_last_send.reset();
              }
            }
            if (!worker->join_frozen_bytes.empty() && aether_runtime) {
              aether_runtime->OpenPeer(worker->runtime->expected_host_uid);
              aether_runtime->SendControl(worker->runtime->expected_host_uid,
                                          worker->join_frozen_bytes);
              worker->join_last_send = std::chrono::steady_clock::now();
            }
          }

          publication_dirty = true;
        });
      },
      /*on_failed=*/
      [this, &worker, epoch](std::string err) {
        EnqueueInternalModelWork([this, &worker, epoch, err = std::move(err)]() {
          AssertModelThread();
          if (!worker || worker->network_epoch != epoch) {
            return;
          }
          UpdateStatus([err](ChatRuntimeStatus& s) {
            s.lifecycle_state = SessionLifecycleState::kFailed;
            s.error_text = err;
          });
        });
      },
      /*on_frame=*/{},
      /*on_presence=*/
      // Presence must mutate remote_presence_map on the model thread only.
      // Full lifecycle ownership lands in Commit 03; enqueue is required for
      // correct OpenPeer/bootstrap sync driving (Online gate).
      [this, &worker, set_peer_presence, epoch](std::string peer_uid,
                                               PeerPresence presence) {
        EnqueueInternalModelWork([this, &worker, set_peer_presence, epoch,
                                  peer_uid = std::move(peer_uid), presence]() {
          AssertModelThread();
          if (!worker || worker->network_epoch != epoch) {
            return;
          }
          set_peer_presence(peer_uid, presence);
        });
      },
      [this, &worker, epoch](bool has_schedule, bool any_online) {
        EnqueueInternalModelWork([this, &worker, epoch, has_schedule, any_online]() {
          AssertModelThread();
          if (!worker || worker->network_epoch != epoch) {
            return;
          }
          LocalConnectivityState const state =
              LocalConnectivityFromDiag(has_schedule, any_online);
          UpdateStatus([state](ChatRuntimeStatus& s) {
            s.local_connectivity = state;
          });
        });
      },
      [this, &worker, epoch](std::string source, std::vector<std::uint8_t> bytes) {
        EnqueueInternalModelWork([this, &worker, epoch, source = std::move(source),
                                  bytes = std::move(bytes)]() mutable {
          AssertModelThread();
          if (!worker || worker->network_epoch != epoch) {
            return;
          }
          if (worker->handle_control) {
            worker->handle_control(std::move(source), std::move(bytes));
          }
        });
      });
  };

  on_retry_connection_ = [this, &worker, &identity_conflict, &aether_runtime,
                          &transport, &sync_runtime, &aether_ready,
                          &remote_presence_map, &start_network]() {
    AssertModelThread();
    if (identity_conflict || !worker || worker->network_retry_in_progress) {
      return;
    }
    SessionLifecycleState life = SessionLifecycleState::kStarting;
    {
      std::lock_guard<std::mutex> lock{status_mu_};
      life = status_.lifecycle_state;
    }
    if (life != SessionLifecycleState::kFailed) {
      return;
    }
    worker->network_retry_in_progress = true;
    ++worker->network_epoch;
    StopEndpointJoin(aether_runtime.get());
    sync_runtime.reset();
    transport.reset();
    aether_runtime.reset();
    aether_ready = false;
    remote_presence_map.clear();
    UpdateStatus([](ChatRuntimeStatus& s) {
      s.lifecycle_state = SessionLifecycleState::kStarting;
      s.local_connectivity = LocalConnectivityState::kUnknown;
      s.remote_presence.clear();
      s.error_text.clear();
    });
    start_network();
    worker->network_retry_in_progress = false;
  };

  start_network();

  auto const refresh_sync_projection =
      [this, &workspace, &my_uid](
          std::unordered_map<std::string, RoomBootstrapState>& bootstrap_out,
          std::map<SharedEventId, MessageDeliveryState>& delivery_out) {
        AssertModelThread();
        bootstrap_out.clear();
        delivery_out.clear();
        if (!workspace.is_valid() || my_uid.empty()) {
          return;
        }
        for (auto const& entry : workspace->chats) {
          if (!entry.is_valid() || !entry->room.is_valid() ||
              !entry->peer_link.is_valid()) {
            continue;
          }
          std::string const& peer_uid = entry->peer_link->EndpointUid();
          if (peer_uid.empty()) {
            continue;
          }
          ae::ObjId remote_share_id;
          for (auto const& share : entry->room->shares) {
            if (share.link.is_valid() && share.link->EndpointUid() == peer_uid) {
              remote_share_id = share.share_id;
              break;
            }
          }
          if (!remote_share_id.is_valid()) {
            continue;
          }
          auto const sync_index =
              entry->room->FindLinkSyncIndexForShare(remote_share_id);
          if (sync_index >= entry->room->link_sync_states.size()) {
            continue;
          }
          auto state = entry->room->link_sync_states[sync_index];
          if (!state.is_valid()) {
            continue;
          }
          if (!state.is_loaded()) {
            state.Load();
          }
          bootstrap_out[peer_uid] =
              BootstrapFromPhase(state->GetInitialSyncPhase());
          for (auto const& message : entry->room->messages) {
            MessageDeliveryState const delivery =
                DeliveryForOwnEvent(*state, message.id, my_uid);
            if (delivery != MessageDeliveryState::kNone) {
              delivery_out[message.id] = delivery;
            }
          }
        }
      };

  // Track sync retry states
  struct SyncRetryState {
    std::chrono::steady_clock::time_point last_attempt{};
  };
  std::unordered_map<std::string, SyncRetryState> retry_states;

  // Main model event loop
  for (;;) {
    std::deque<ModelWork> local_work;
    {
      std::unique_lock<std::mutex> lock{queue_mu_};
      queue_cv_.wait_for(lock, std::chrono::milliseconds(50), [&]() {
        return stop_requested_.load(std::memory_order_relaxed) ||
               !work_queue_.empty();
      });
      if (stop_requested_.load(std::memory_order_relaxed) &&
          work_queue_.empty()) {
        break;
      }
      local_work.swap(work_queue_);
    }

    // Execute queued work
    while (!local_work.empty()) {
      auto work = std::move(local_work.front());
      local_work.pop_front();
      if (work) {
        work();
      }
    }

    if (publication_dirty) {
      std::lock_guard<std::mutex> pub_lock{publication_mu_};
      // Busy GUI: retain dirty; do not wait on the publication slot.
      if (!channel_.is_publication_busy() && workspace.is_valid()) {
        publish_now_unlocked(/*is_initial=*/false);
        publication_dirty = false;
      }
    }

    if (stop_requested_.load(std::memory_order_relaxed)) {
      continue;
    }

    auto const now = std::chrono::steady_clock::now();

    if (sync_runtime && workspace.is_valid() && !identity_conflict) {
      for (auto const& entry : workspace->chats) {
        if (!entry.is_valid() || !entry->room.is_valid() || !entry->peer_link.is_valid()) {
          continue;
        }
        std::string const& peer_uid = entry->peer_link->EndpointUid();
        if (peer_uid.empty()) {
          continue;
        }

        auto room = entry->room;
        ae::ObjId remote_share_id;
        for (auto const& share : room->shares) {
          if (share.link.is_valid() && share.link->EndpointUid() == peer_uid) {
            remote_share_id = share.share_id;
            break;
          }
        }
        if (!remote_share_id.is_valid()) {
          continue;
        }

        auto const sync_index = room->FindLinkSyncIndexForShare(remote_share_id);
        if (sync_index >= room->link_sync_states.size()) {
          continue;
        }
        auto state = room->link_sync_states[sync_index];
        if (!state.is_valid()) {
          continue;
        }
        if (!state.is_loaded()) {
          state.Load();
        }

        auto& retry = retry_states[peer_uid];
        auto const phase = state->GetInitialSyncPhase();
        auto const before = CaptureRoomSyncSnapshot(*room, *state);

        // Initial snapshot retry must not wait for Online: Join introduces the
        // peer, and control/data frames can reorder relative to presence.
        if (phase == apptraverse::InitialSyncPhase::NotStarted) {
          sync_runtime->SyncInitialState(room.id(), remote_share_id);
          retry.last_attempt = now;
        } else if (phase == apptraverse::InitialSyncPhase::Pending) {
          if (now - retry.last_attempt >= std::chrono::seconds(1)) {
            sync_runtime->SyncInitialState(room.id(), remote_share_id);
            retry.last_attempt = now;
          }
        } else if (phase == apptraverse::InitialSyncPhase::Complete) {
          // Incremental events: only when peer is Online.
          auto pres_it = remote_presence_map.find(peer_uid);
          if (pres_it == remote_presence_map.end() ||
              pres_it->second != PeerPresence::kOnline) {
            continue;
          }
          if (state->HasPendingEvent()) {
            if (now - retry.last_attempt >= std::chrono::seconds(1)) {
              sync_runtime->SyncNextEvent(room.id(), remote_share_id);
              retry.last_attempt = now;
            }
          } else {
            sync_runtime->SyncNextEvent(room.id(), remote_share_id);
            retry.last_attempt = now;
          }
        }

        if (!state.is_loaded()) {
          state.Load();
        }
        auto const after = CaptureRoomSyncSnapshot(*room, *state);
        if (before != after) {
          persist_workspace();
          publication_dirty = true;
        }
      }

      std::unordered_map<std::string, RoomBootstrapState> bootstrap_projection;
      std::map<SharedEventId, MessageDeliveryState> delivery_projection;
      refresh_sync_projection(bootstrap_projection, delivery_projection);
      bool status_changed = false;
      {
        std::lock_guard<std::mutex> lock{status_mu_};
        status_changed = status_.room_bootstrap_by_peer_uid != bootstrap_projection ||
                         status_.delivery_by_event_id != delivery_projection;
      }
      if (status_changed) {
        UpdateStatus([&bootstrap_projection, &delivery_projection](
                         ChatRuntimeStatus& s) {
          s.room_bootstrap_by_peer_uid = std::move(bootstrap_projection);
          s.delivery_by_event_id = std::move(delivery_projection);
        });
      }
    }

    // Join retry is independent of bound-room Online sync driving.
    drive_join_send(now);
    project_demo_status();
  }

    ShutdownEndpointAndDrain(aether_runtime.get());

    {
      std::lock_guard<std::mutex> lock{queue_mu_};
      accepting_internal_delivery_ = false;
    }

    DrainModelQueue();

    persist_workspace();

    sync_runtime.reset();
    transport.reset();
    aether_runtime.reset();
    workspace = {};
    worker.reset();

  } catch (std::exception const& ex) {
    worker_failed = true;
    worker_error = ex.what();
    UpdateStatus([&worker_error](ChatRuntimeStatus& s) {
      s.lifecycle_state = SessionLifecycleState::kFailed;
      s.error_text = worker_error;
    });
    // Keep WorkerState alive: stop producers, then discard unsafe queued work
    // instead of executing callbacks against a failed model.
    if (worker) {
      StopEndpointJoin(worker->endpoint.get());
    }
    DiscardModelQueue();
    if (worker) {
      worker->sync_runtime.reset();
      worker->transport.reset();
      worker->endpoint.reset();
      worker->workspace = {};
      worker.reset();
    }
  } catch (...) {
    worker_failed = true;
    UpdateStatus([](ChatRuntimeStatus& s) {
      s.lifecycle_state = SessionLifecycleState::kFailed;
      s.error_text = "ChatSession worker failed with unknown error";
    });
    if (worker) {
      StopEndpointJoin(worker->endpoint.get());
    }
    DiscardModelQueue();
    if (worker) {
      worker->sync_runtime.reset();
      worker->transport.reset();
      worker->endpoint.reset();
      worker->workspace = {};
      worker.reset();
    }
  }

  finalize_worker();
}

}  // namespace apptraverse::example::chat_demo
