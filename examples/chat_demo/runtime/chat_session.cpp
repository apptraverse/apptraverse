#include "chat_session.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <utility>

#include "aether/types/uid.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync_runtime.h"

#include "aether_byte_transport.h"
#include "aether_link.h"
#include "chat_aether_runtime.h"
#include "chat_commands.h"

namespace apptraverse::example::chat_demo {
namespace {

inline constexpr ae::ObjId kLocalWorkspaceRootId{10001};

std::string CanonicalizeEndpoint(std::string const& raw) {
  if (raw.empty()) {
    return "";
  }
  auto uid = ae::Uid::FromString(raw);
  if (uid.empty()) {
    return "";
  }
  return ae::Format("{}", uid);
}

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
  stop_ = false;
  notify_ui_ = std::move(notify_ui);
  worker_thread_ = std::thread([this, config = std::move(config), notify = notify_ui_]() mutable {
    ThreadMain(std::move(config), std::move(notify));
  });
  return true;
}

void ChatSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{queue_mu_};
    if (!started_ || stop_) {
      return;
    }
    stop_ = true;
  }
  queue_cv_.notify_all();
}

void ChatSession::Join() {
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

void ChatSession::EnqueueModelWork(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{queue_mu_};
    if (stop_) {
      return;
    }
    work_queue_.push_back(std::move(work));
  }
  queue_cv_.notify_all();
}

ChatRuntimeStatus ChatSession::GetRuntimeStatus() {
  std::lock_guard<std::mutex> lock{status_mu_};
  return status_;
}

void ChatSession::UpdateStatus(std::function<void(ChatRuntimeStatus&)> mutator) {
  {
    std::lock_guard<std::mutex> lock{status_mu_};
    mutator(status_);
  }
  if (notify_ui_) {
    notify_ui_();
  }
}

void ChatSession::OpenPeer(OpenPeerRequest request) {
  EnqueueModelWork([this, req = std::move(request)]() mutable {
    if (on_open_peer_) {
      on_open_peer_(std::move(req));
    }
  });
}

void ChatSession::SelectChat(ae::ObjId entry_id) {
  EnqueueModelWork([this, entry_id]() {
    if (on_select_chat_) {
      on_select_chat_(entry_id);
    }
  });
}

void ChatSession::EditDraft(ae::ObjId entry_id, std::string text,
                            std::uint64_t edit_revision) {
  EnqueueModelWork([this, entry_id, text = std::move(text), edit_revision]() mutable {
    if (on_edit_draft_) {
      on_edit_draft_(entry_id, std::move(text), edit_revision);
    }
  });
}

void ChatSession::SendDraft(ae::ObjId entry_id, std::string current_text,
                            std::uint64_t edit_revision) {
  EnqueueModelWork([this, entry_id, current_text = std::move(current_text),
                    edit_revision]() mutable {
    if (on_send_draft_) {
      on_send_draft_(entry_id, std::move(current_text), edit_revision);
    }
  });
}

void ChatSession::SaveScroll(ae::ObjId entry_id, ScrollAnchor anchor) {
  EnqueueModelWork([this, entry_id, anchor]() {
    if (on_save_scroll_) {
      on_save_scroll_(entry_id, anchor);
    }
  });
}

void ChatSession::SaveBounds(DesktopBounds bounds) {
  EnqueueModelWork([this, bounds]() {
    if (on_save_bounds_) {
      on_save_bounds_(bounds);
    }
  });
}

void ChatSession::ThreadMain(ChatSessionConfig config, UiNotifyFn notify_ui) {
  // 1. Register all chat and AetherLink classes on model thread
  apptraverse::EnsureObjectRegistration();
  EnsureChatDemoModelRegistration();
  EnsureAetherLinkRegistration();

  auto const model_dir = config.state_dir / "model";
  auto const aether_dir = config.state_dir / "aether";
  std::filesystem::create_directories(model_dir);
  std::filesystem::create_directories(aether_dir);

  // 2. Open DirectoryDomainStorage at state_dir/model
  apptraverse::DirectoryDomainStorage storage{model_dir};
  ae::Domain domain{storage};

  // 3 & 4. Load or create ChatWorkspace
  ChatWorkspace::ptr workspace;
  if (!storage.Enumerate(kLocalWorkspaceRootId).empty()) {
    workspace = ChatWorkspace::ptr::Declare(ae::CreateWith{domain}.with_id(kLocalWorkspaceRootId));
    workspace.Load();
    if (!workspace) {
      UpdateStatus([](ChatRuntimeStatus& s) {
        s.lifecycle_state = SessionLifecycleState::kFailed;
        s.error_text = "Failed to load existing workspace root";
      });
      return;
    }
  } else {
    workspace = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(kLocalWorkspaceRootId));
    InitializeRuntimeNode(*workspace);
    workspace.Save();
  }

  auto const persist_workspace = [&workspace]() {
    if (workspace.is_valid()) {
      workspace.Save();
      for (auto& entry : workspace->chats) {
        if (entry.is_valid()) {
          entry.Save();
          if (entry->peer_link.is_valid()) {
            entry->peer_link.Save();
          }
          if (entry->room.is_valid()) {
            entry->room.Save();
            for (auto& s : entry->room->link_sync_states) {
              if (s.is_valid()) {
                s.Save();
              }
            }
          }
        }
      }
    }
  };

  // Ensure every chat room blocks journal compaction for demo retry/dedup
  for (auto const& entry : workspace->chats) {
    if (entry.is_valid() && entry->room.is_valid()) {
      entry->room->SetJournalCompactionBlocked(true);
    }
  }

  bool publication_dirty = false;
  auto const publish_now = [this, &workspace, &notify_ui, &publication_dirty](bool is_initial) {
    if (!workspace.is_valid()) {
      return;
    }
    if (!channel_.is_publication_busy()) {
      auto* buf = channel_.AcquireProducer();
      if (is_initial) {
        SerializeInitialPublication(*workspace, buf->sink);
      } else {
        SerializeStructuralNodePublication(*workspace, buf->sink);
      }
      channel_.NotePublished();
      channel_.PublishProducer();
      publication_dirty = false;
      if (notify_ui) {
        notify_ui();
      }
    } else {
      publication_dirty = true;
    }
  };

  // 5. Publish local workspace immediately before network registration
  publish_now(/*is_initial=*/true);

  // 6. Start network endpoint (default: ChatAetherRuntime) on model thread
  std::unique_ptr<IAetherFrameEndpoint> aether_runtime = endpoint_factory_();
  assert(aether_runtime && "EndpointFactory must create one endpoint");
  IAetherFrameEndpoint::Config aether_cfg{
      .state_dir = aether_dir,
      .client_name = config.aether_client_name,
      .heartbeat_period_ms = 1000,
      .offline_after_ms = 4000,
  };

  std::string my_uid;
  bool aether_ready = false;
  std::unique_ptr<AetherByteTransport> transport;
  std::unique_ptr<SharedSyncRuntime> sync_runtime;

  std::unordered_map<std::string, PeerPresence> remote_presence_map;

  // Runtime presence helper
  auto const set_peer_presence = [this, &remote_presence_map, &publication_dirty](
                                     std::string const& peer, PeerPresence p) {
    remote_presence_map[peer] = p;
    UpdateStatus([&remote_presence_map](ChatRuntimeStatus& s) {
      s.remote_presence = remote_presence_map;
    });
    publication_dirty = true;
  };

  // Dispatcher for incoming tasks from Aether thread
  auto const model_dispatcher = [this](ModelTask task) {
    EnqueueModelWork([t = std::move(task)]() mutable {
      if (t) {
        t();
      }
    });
  };

  // Setup resolution and open peer helper
  auto const resolve_and_open_peer =
      [&workspace, &aether_runtime, &sync_runtime, &persist_workspace,
       &my_uid, &publication_dirty, this](OpenPeerRequest const& req) {
        std::string const admin_id = req.peer_admin_id;
        auto entry = OpenOrSelectChat(*workspace, admin_id,
                                      req.peer_name.value_or(""),
                                      persist_workspace);
        if (!entry.is_valid()) {
          return;
        }

        std::string target_endpoint;
        if (entry->peer_link.is_valid() && !entry->peer_link->EndpointUid().empty()) {
          target_endpoint = entry->peer_link->EndpointUid();
        } else if (req.peer_aether_uid.has_value()) {
          std::string canonical = CanonicalizeEndpoint(*req.peer_aether_uid);
          if (canonical.empty() || (!my_uid.empty() && canonical == my_uid)) {
            UpdateStatus([](ChatRuntimeStatus& s) {
              s.error_text = "Invalid or self Aether UID";
            });
            publication_dirty = true;
            return;
          }
          target_endpoint = canonical;
        }

        if (target_endpoint.empty()) {
          UpdateStatus([](ChatRuntimeStatus& s) {
            s.error_text = "Aether UID required";
          });
          publication_dirty = true;
          return;
        }

        // Bound entry conflict check
        if (entry->peer_link.is_valid() && entry->peer_link->EndpointUid() != target_endpoint) {
          UpdateStatus([](ChatRuntimeStatus& s) {
            s.error_text = "Endpoint conflict for bound chat";
          });
          publication_dirty = true;
          return;
        }

        aether_runtime->OpenPeer(target_endpoint);

        if (entry->room.is_valid()) {
          // Restored or already created room: reuse
          if (sync_runtime) {
            sync_runtime->RegisterNode(entry->room);
          }
          publication_dirty = true;
          return;
        }

        // Creator deterministic selection: canonical local UID < canonical remote UID
        if (my_uid.empty()) {
          return;
        }

        if (my_uid < target_endpoint) {
          // Local side is Creator
          auto room = ChatRoom::ptr::Create(ae::CreateWith{*workspace->domain});
          InitializeRuntimeNode(*room);
          room->SetJournalCompactionBlocked(true);

          auto local_link = AetherLink::ptr::Create(ae::CreateWith{*workspace->domain});
          local_link->endpoint_uid = my_uid;
          InitializeRuntimeNode(*local_link);

          auto remote_link = AetherLink::ptr::Create(ae::CreateWith{*workspace->domain});
          remote_link->endpoint_uid = target_endpoint;
          InitializeRuntimeNode(*remote_link);

          room->AddShare(local_link, apptraverse::ShareAccess::ReadWrite);
          room->AddShare(remote_link, apptraverse::ShareAccess::ReadWrite);

          BindChat(*entry, remote_link, room, persist_workspace);
          if (sync_runtime) {
            sync_runtime->RegisterNode(room);
          }
          persist_workspace();
          publication_dirty = true;
        } else {
          // Waiting side: authorize initial node from target endpoint
          if (sync_runtime) {
            sync_runtime->ExpectInitialNodeFromEndpoint(target_endpoint, ChatRoom::kClassId);
          }
          publication_dirty = true;
        }
      };

  std::optional<OpenPeerRequest> pending_initial_request = config.initial_open_peer;

  // Handlers for GUI commands
  on_open_peer_ = [&resolve_and_open_peer](OpenPeerRequest req) {
    resolve_and_open_peer(req);
  };

  on_select_chat_ = [&workspace, &persist_workspace, &publication_dirty](ae::ObjId entry_id) {
    if (apptraverse::example::chat_demo::SelectChat(*workspace, entry_id, persist_workspace)) {
      publication_dirty = true;
    }
  };

  on_edit_draft_ = [&workspace, &persist_workspace, &publication_dirty, this](
                       ae::ObjId entry_id, std::string text, std::uint64_t edit_revision) {
    for (auto const& entry : workspace->chats) {
      if (entry.is_valid() && entry.id() == entry_id) {
        if (SetDraft(*entry, text, persist_workspace)) {
          UpdateStatus([edit_revision](ChatRuntimeStatus& s) {
            s.processed_edit_revision = edit_revision;
          });
          publication_dirty = true;
        }
        break;
      }
    }
  };

  on_send_draft_ = [&workspace, &persist_workspace, &publication_dirty, this](
                       ae::ObjId entry_id, std::string current_text, std::uint64_t edit_revision) {
    for (auto const& entry : workspace->chats) {
      if (entry.is_valid() && entry.id() == entry_id) {
        SetDraft(*entry, current_text, persist_workspace);
        auto now_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        auto msg_id = SubmitDraft(*workspace, *entry, now_us, persist_workspace);
        if (!msg_id.origin_uid.empty() && msg_id.origin_sequence > 0) {
          UpdateStatus([edit_revision](ChatRuntimeStatus& s) {
            s.processed_edit_revision = edit_revision;
          });
          publication_dirty = true;
        }
        break;
      }
    }
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

  aether_runtime->Start(
      std::move(aether_cfg),
      /*on_uid=*/
      [this, &workspace, &persist_workspace, &my_uid, &publication_dirty](std::string uid) {
        EnqueueModelWork([this, &workspace, &persist_workspace, &my_uid, &publication_dirty,
                          uid = std::move(uid)]() {
          my_uid = uid;
          BindLocalEndpoint(*workspace, my_uid, persist_workspace);
          UpdateStatus([&my_uid](ChatRuntimeStatus& s) {
            s.local_endpoint_uid = my_uid;
          });
          publication_dirty = true;
        });
      },
      /*on_ready=*/
      [this, &aether_ready, &workspace, &aether_runtime, &transport, &sync_runtime,
       &domain, &storage, &my_uid, &model_dispatcher, &persist_workspace,
       &resolve_and_open_peer, &pending_initial_request, &publication_dirty]() {
        EnqueueModelWork([this, &aether_ready, &workspace, &aether_runtime, &transport,
                          &sync_runtime, &domain, &storage, &my_uid, &model_dispatcher,
                          &persist_workspace, &resolve_and_open_peer, &pending_initial_request,
                          &publication_dirty]() {
          aether_ready = true;
          UpdateStatus([](ChatRuntimeStatus& s) {
            s.lifecycle_state = SessionLifecycleState::kReady;
          });

          // 8. Create AetherByteTransport and SharedSyncRuntime
          transport = std::make_unique<AetherByteTransport>(
              *aether_runtime, my_uid, model_dispatcher);
          sync_runtime = std::make_unique<SharedSyncRuntime>(domain, storage, *transport);

          // 9. Allow MessageAddedEvent
          sync_runtime->AllowStandaloneEventClass(MessageAddedEvent::kClassId);

          // Configure imported callback for unknown node admission
          sync_runtime->SetInitialNodeImportedCallback(
              [&workspace, &persist_workspace, &publication_dirty](
                  std::string const& source_endpoint, SharedNode::ptr imported_node) -> bool {
                if (!imported_node.is_valid()) {
                  return false;
                }
                auto room = ChatRoom::ptr::MakeFromThis(
                    static_cast<ChatRoom*>(&*imported_node));
                if (!room.is_valid()) {
                  return false;
                }
                room->SetJournalCompactionBlocked(true);

                // Find waiting entry for source_endpoint
                ChatEntry::ptr waiting_entry;
                for (auto const& entry : workspace->chats) {
                  if (entry.is_valid() && !entry->room.is_valid()) {
                    if (entry->peer_link.is_valid() &&
                        entry->peer_link->EndpointUid() == source_endpoint) {
                      waiting_entry = entry;
                      break;
                    }
                  }
                }

                if (!waiting_entry.is_valid()) {
                  return false;
                }

                // Find imported Share whose Link endpoint equals source_endpoint
                apptraverse::Link::ptr remote_link;
                for (auto const& share : room->shares) {
                  if (share.link.is_valid() && share.link->EndpointUid() == source_endpoint) {
                    remote_link = share.link;
                    break;
                  }
                }

                if (!remote_link.is_valid()) {
                  return false;
                }

                if (!BindChat(*waiting_entry, remote_link, room, persist_workspace)) {
                  return false;
                }
                persist_workspace();
                publication_dirty = true;
                return true;
              });

          // 10. Register every already-bound ChatRoom from restored workspace
          for (auto const& entry : workspace->chats) {
            if (entry.is_valid() && entry->room.is_valid()) {
              sync_runtime->RegisterNode(entry->room);
            }
          }

          // 11. Reopen peers from their persistent Link descriptors
          for (auto const& entry : workspace->chats) {
            if (entry.is_valid() && entry->peer_link.is_valid()) {
              std::string const& uid = entry->peer_link->EndpointUid();
              if (!uid.empty()) {
                aether_runtime->OpenPeer(uid);
              }
            }
          }

          // 12. Apply initial OpenPeerRequest if present
          if (pending_initial_request.has_value()) {
            resolve_and_open_peer(*pending_initial_request);
            pending_initial_request.reset();
          }

          publication_dirty = true;
        });
      },
      /*on_failed=*/
      [this](std::string err) {
        EnqueueModelWork([this, err = std::move(err)]() {
          UpdateStatus([err](ChatRuntimeStatus& s) {
            s.lifecycle_state = SessionLifecycleState::kFailed;
            s.error_text = err;
          });
        });
      },
      /*on_frame=*/{},
      /*on_presence=*/
      [set_peer_presence](std::string peer_uid, PeerPresence presence) {
        set_peer_presence(peer_uid, presence);
      });

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
        return stop_ || !work_queue_.empty();
      });
      if (stop_ && work_queue_.empty()) {
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

    // Attempt to publish coalesced structural changes if channel is ready
    if (publication_dirty && !channel_.is_publication_busy()) {
      publish_now(/*is_initial=*/false);
    }

    auto const now = std::chrono::steady_clock::now();

    // Drive sync engine if sync_runtime is active
    if (sync_runtime && workspace.is_valid()) {
      for (auto const& entry : workspace->chats) {
        if (!entry.is_valid() || !entry->room.is_valid() || !entry->peer_link.is_valid()) {
          continue;
        }
        std::string const& peer_uid = entry->peer_link->EndpointUid();
        if (peer_uid.empty()) {
          continue;
        }

        // Only drive sync when peer is Online
        auto pres_it = remote_presence_map.find(peer_uid);
        if (pres_it == remote_presence_map.end() || pres_it->second != PeerPresence::kOnline) {
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

        if (phase == apptraverse::InitialSyncPhase::NotStarted) {
          sync_runtime->SyncInitialState(room.id(), remote_share_id);
          retry.last_attempt = now;
          persist_workspace();
          publication_dirty = true;
        } else if (phase == apptraverse::InitialSyncPhase::Pending) {
          if (now - retry.last_attempt >= std::chrono::seconds(1)) {
            sync_runtime->SyncInitialState(room.id(), remote_share_id);
            retry.last_attempt = now;
          }
        } else if (phase == apptraverse::InitialSyncPhase::Complete) {
          if (state->HasPendingEvent()) {
            if (now - retry.last_attempt >= std::chrono::seconds(1)) {
              sync_runtime->SyncNextEvent(room.id(), remote_share_id);
              retry.last_attempt = now;
            }
          } else {
            sync_runtime->SyncNextEvent(room.id(), remote_share_id);
            retry.last_attempt = now;
            persist_workspace();
            publication_dirty = true;
          }
        }
      }
    }
  }

  // Model shutdown sequence:
  // 1. Stop accepting new USER commands (done by stop_ flag)
  // 2. RequestStop and Join ChatAetherRuntime while model queue still exists
  aether_runtime->RequestStop();
  aether_runtime->Join();

  // 3. Drain already queued model deliveries
  std::deque<ModelWork> remaining_work;
  {
    std::lock_guard<std::mutex> lock{queue_mu_};
    remaining_work.swap(work_queue_);
  }
  while (!remaining_work.empty()) {
    auto work = std::move(remaining_work.front());
    remaining_work.pop_front();
    if (work) {
      work();
    }
  }

  // 4. Save workspace
  persist_workspace();

  // 5. Destroy SharedSyncRuntime on model thread
  sync_runtime.reset();

  // 6. Destroy AetherByteTransport while endpoint still exists
  transport.reset();

  // 7. Destroy chat objects and clear domain/storage
  workspace = {};

  // 8. Notify GUI completion
  UpdateStatus([](ChatRuntimeStatus& s) {
    s.lifecycle_state = SessionLifecycleState::kStopped;
  });
}

}  // namespace apptraverse::example::chat_demo
