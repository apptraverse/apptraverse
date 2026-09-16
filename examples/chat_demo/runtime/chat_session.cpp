#include "chat_session.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <deque>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

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
#include "chat_commands.h"

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
    if (!started_ || stop_) {
      return;
    }
    stop_ = true;
    accepting_user_commands_ = false;
  }
  queue_cv_.notify_all();
}

void ChatSession::Join() {
  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
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

ChatRuntimeStatus ChatSession::GetRuntimeStatus() {
  std::lock_guard<std::mutex> lock{status_mu_};
  return status_;
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

void ChatSession::ShutdownEndpointAndDrain(IAetherFrameEndpoint* endpoint) {
  if (endpoint != nullptr) {
    endpoint->RequestStop();
    endpoint->Join();
  }
  DrainModelQueue();
}

void ChatSession::OpenPeer(OpenPeerRequest request) {
  EnqueueUserModelWork([this, req = std::move(request)]() mutable {
    AssertModelThread();
    if (on_open_peer_) {
      on_open_peer_(std::move(req));
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

void ChatSession::ThreadMain(ChatSessionConfig config, UiNotifyFn notify_ui) {
  model_thread_id_ = std::this_thread::get_id();

  bool worker_failed = false;
  std::string worker_error;

  std::unique_ptr<IAetherFrameEndpoint> aether_runtime;
  std::unique_ptr<AetherByteTransport> transport;
  std::unique_ptr<SharedSyncRuntime> sync_runtime;
  ChatWorkspace::ptr workspace;

  auto const finalize_worker = [this, &worker_failed]() {
    on_open_peer_ = nullptr;
    on_select_chat_ = nullptr;
    on_edit_draft_ = nullptr;
    on_send_draft_ = nullptr;
    on_save_scroll_ = nullptr;
    on_save_bounds_ = nullptr;

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
  };

  try {
    apptraverse::EnsureObjectRegistration();
    EnsureChatDemoModelRegistration();
    EnsureAetherLinkRegistration();

    auto const model_dir = config.state_dir / "model";
    auto const aether_dir = config.state_dir / "aether";
    std::filesystem::create_directories(model_dir);
    std::filesystem::create_directories(aether_dir);

    apptraverse::DirectoryDomainStorage storage{model_dir};
    ae::Domain domain{storage};

    if (!storage.Enumerate(kLocalWorkspaceRootId).empty()) {
      workspace = ChatWorkspace::ptr::Declare(
          ae::CreateWith{domain}.with_id(kLocalWorkspaceRootId));
      workspace.Load();
      if (!workspace) {
        throw std::runtime_error("Failed to load existing workspace root");
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

    aether_runtime = endpoint_factory_();
    assert(aether_runtime && "EndpointFactory must create one endpoint");
  IAetherFrameEndpoint::Config aether_cfg{
      .state_dir = aether_dir,
      .client_name = config.aether_client_name,
      .heartbeat_period_ms = 1000,
      .offline_after_ms = 4000,
  };

    std::string my_uid;
    bool aether_ready = false;
    bool identity_conflict = false;

    std::unordered_map<std::string, PeerPresence> remote_presence_map;

  // Model-thread-only: pending UID hints and waiting-side endpoint -> entry.
  // Values/IDs only; bound Link remains authoritative after BindChat.
  std::map<ae::ObjId, std::string> endpoint_by_pending_entry;
  std::map<std::string, ae::ObjId> waiting_entry_by_endpoint;
  std::deque<OpenPeerRequest> deferred_open_while_registering;


  // Runtime presence helper
    auto const set_peer_presence =
        [this, &remote_presence_map, &publication_dirty](std::string const& peer,
                                                         PeerPresence p) {
          AssertModelThread();
          remote_presence_map[peer] = p;
    UpdateStatus([&remote_presence_map](ChatRuntimeStatus& s) {
      s.remote_presence = remote_presence_map;
    });
    publication_dirty = true;
  };

    auto const model_dispatcher = [this](ModelTask task) {
      EnqueueInternalModelWork([t = std::move(task)]() mutable {
      if (t) {
        t();
      }
    });
  };

    auto const resolve_and_open_peer =
        [&workspace, &aether_runtime, &sync_runtime, &persist_workspace, &my_uid,
         &aether_ready, &publication_dirty, &endpoint_by_pending_entry,
         &waiting_entry_by_endpoint, &deferred_open_while_registering,
         &identity_conflict, this](OpenPeerRequest const& req) {
          AssertModelThread();
          if (identity_conflict) {
            return;
          }

        // 1. Normalize Admin ID and open/select chat (works before readiness).
        std::string const admin_id = TrimAsciiWhitespace(req.peer_admin_id);
        auto entry = OpenOrSelectChat(*workspace, admin_id,
                                      req.peer_name.value_or(""),
                                      persist_workspace);
        if (!entry.is_valid()) {
          return;
        }

        // 2–3. Validate any supplied UID hint before Aether parsing.
        std::optional<std::string> hint_canonical;
        if (req.peer_aether_uid.has_value()) {
          std::string canonical;
          if (!TryCanonicalizeAetherUid(*req.peer_aether_uid, canonical)) {
            UpdateStatus([](ChatRuntimeStatus& s) {
              s.error_text = "Invalid Aether UID";
            });
            publication_dirty = true;
            return;
          }
          hint_canonical = std::move(canonical);
        }

        bool const bound =
            entry->peer_link.is_valid() &&
            !entry->peer_link->EndpointUid().empty();

        // 4. Bound entry: supplied hint must match the bound endpoint.
        if (bound && hint_canonical.has_value() &&
            *hint_canonical != entry->peer_link->EndpointUid()) {
          UpdateStatus([](ChatRuntimeStatus& s) {
            s.error_text = "Endpoint conflict for bound chat";
          });
          publication_dirty = true;
          return;
        }

        std::string target_endpoint;
        if (bound) {
          target_endpoint = entry->peer_link->EndpointUid();
        } else if (hint_canonical.has_value()) {
          target_endpoint = *hint_canonical;
        } else {
          auto pending = endpoint_by_pending_entry.find(entry.id());
          if (pending != endpoint_by_pending_entry.end()) {
            target_endpoint = pending->second;
          }
        }

        // Alias: endpoint already bound under a different Admin ID.
        if (!target_endpoint.empty()) {
          for (auto const& other : workspace->chats) {
            if (!other.is_valid() || other.id() == entry.id()) {
              continue;
            }
            if (other->peer_link.is_valid() &&
                other->peer_link->EndpointUid() == target_endpoint) {
              if (other->peer_admin_id != entry->peer_admin_id) {
                UpdateStatus([](ChatRuntimeStatus& s) {
                  s.error_text =
                      "Admin ID alias conflict for existing endpoint";
                });
                apptraverse::example::chat_demo::SelectChat(
                    *workspace, other.id(), persist_workspace);
                publication_dirty = true;
                return;
              }
            }
          }
          auto waiting = waiting_entry_by_endpoint.find(target_endpoint);
          if (waiting != waiting_entry_by_endpoint.end() &&
              waiting->second != entry.id()) {
            UpdateStatus([](ChatRuntimeStatus& s) {
              s.error_text = "Endpoint already waiting on another chat";
            });
            publication_dirty = true;
            return;
          }
        }

        // 5. Reject self-connection once local UID is known.
        if (!target_endpoint.empty() && !my_uid.empty() &&
            target_endpoint == my_uid) {
          UpdateStatus([](ChatRuntimeStatus& s) {
            s.error_text = "Cannot open chat with self Aether UID";
          });
          publication_dirty = true;
          return;
        }

        // 6. Unbound with valid hint: retain pending map.
        if (!bound && hint_canonical.has_value()) {
          endpoint_by_pending_entry[entry.id()] = *hint_canonical;
          target_endpoint = *hint_canonical;
        }

        // 7. Not ready yet: retain request and return.
        if (!aether_ready || my_uid.empty() || !sync_runtime) {
          if (target_endpoint.empty()) {
            UpdateStatus([](ChatRuntimeStatus& s) {
              s.error_text = "Aether UID required";
            });
            publication_dirty = true;
            return;
          }
          deferred_open_while_registering.push_back(req);
          publication_dirty = true;
          return;
        }

        if (target_endpoint.empty()) {
          UpdateStatus([](ChatRuntimeStatus& s) {
            s.error_text = "Aether UID required";
          });
          publication_dirty = true;
          return;
        }

        aether_runtime->OpenPeer(target_endpoint);

        if (entry->room.is_valid()) {
          // 8. Restored room: register only when not already registered.
          auto existing = sync_runtime->FindNode(entry->room.id());
          if (!existing.is_valid()) {
            sync_runtime->RegisterNode(entry->room);
          } else if (existing.id() != entry->room.id() ||
                     &*existing != &*entry->room) {
            UpdateStatus([](ChatRuntimeStatus& s) {
              s.error_text = "Conflicting SharedNode registration";
            });
            publication_dirty = true;
            return;
          }
          publication_dirty = true;
          return;
        }

        // 9. Creator election by canonical UID ordering.
        if (my_uid < target_endpoint) {
          auto room = ChatRoom::ptr::Create(ae::CreateWith{*workspace->domain});
          InitializeRuntimeNode(*room);
          room->SetJournalCompactionBlocked(true);

          auto local_link =
              AetherLink::ptr::Create(ae::CreateWith{*workspace->domain});
          local_link->endpoint_uid = my_uid;
          InitializeRuntimeNode(*local_link);

          auto remote_link =
              AetherLink::ptr::Create(ae::CreateWith{*workspace->domain});
          remote_link->endpoint_uid = target_endpoint;
          InitializeRuntimeNode(*remote_link);

          room->AddShare(local_link, apptraverse::ShareAccess::ReadWrite);
          room->AddShare(remote_link, apptraverse::ShareAccess::ReadWrite);

          BindChat(*entry, remote_link, room, persist_workspace);
          sync_runtime->RegisterNode(room);
          endpoint_by_pending_entry.erase(entry.id());
          waiting_entry_by_endpoint.erase(target_endpoint);
          persist_workspace();
          publication_dirty = true;
        } else {
          // 10. Waiting side: map endpoint BEFORE ExpectInitialNodeFromEndpoint.
          // No placeholder peer Link.
          waiting_entry_by_endpoint[target_endpoint] = entry.id();
          sync_runtime->ExpectInitialNodeFromEndpoint(target_endpoint,
                                                      ChatRoom::kClassId);
          publication_dirty = true;
        }
      };

  if (config.initial_open_peer.has_value()) {
    deferred_open_while_registering.push_back(*config.initial_open_peer);
  }

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
    AssertModelThread();
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
    AssertModelThread();
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
      [this, &workspace, &persist_workspace, &my_uid, &publication_dirty,
       &identity_conflict, &aether_runtime](std::string uid) {
        EnqueueInternalModelWork([this, &workspace, &persist_workspace, &my_uid,
                                  &publication_dirty, &identity_conflict,
                                  &aether_runtime, uid = std::move(uid)]() mutable {
          AssertModelThread();
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
      [this, &aether_ready, &workspace, &aether_runtime, &transport, &sync_runtime,
       &domain, &storage, &my_uid, &model_dispatcher, &persist_workspace,
       &resolve_and_open_peer, &deferred_open_while_registering, &publication_dirty,
       &endpoint_by_pending_entry, &waiting_entry_by_endpoint,
       &identity_conflict]() {
        EnqueueInternalModelWork([this, &aether_ready, &workspace, &aether_runtime,
                                  &transport, &sync_runtime, &domain, &storage,
                                  &my_uid, &model_dispatcher, &persist_workspace,
                                  &resolve_and_open_peer,
                                  &deferred_open_while_registering, &publication_dirty,
                                  &endpoint_by_pending_entry, &waiting_entry_by_endpoint,
                                  &identity_conflict]() {
          AssertModelThread();
          if (identity_conflict) {
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
               &waiting_entry_by_endpoint, &endpoint_by_pending_entry](
                  std::string const& source_endpoint,
                  SharedNode::ptr imported_node) -> bool {
                if (!imported_node.is_valid()) {
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
                room->SetJournalCompactionBlocked(true);

                auto waiting_it =
                    waiting_entry_by_endpoint.find(source_endpoint);
                if (waiting_it == waiting_entry_by_endpoint.end()) {
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
                if (waiting_entry->room.is_valid() &&
                    waiting_entry->room.id() == room.id()) {
                  // Idempotent re-bind after a prior failed callback attempt.
                  endpoint_by_pending_entry.erase(waiting_entry.id());
                  waiting_entry_by_endpoint.erase(source_endpoint);
                  publication_dirty = true;
                  return true;
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

                if (!BindChat(*waiting_entry, remote_link, room,
                              persist_workspace)) {
                  return false;
                }
                persist_workspace();
                endpoint_by_pending_entry.erase(waiting_entry.id());
                waiting_entry_by_endpoint.erase(source_endpoint);
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

          for (auto const& entry : workspace->chats) {
            if (entry.is_valid() && entry->peer_link.is_valid()) {
              std::string const& uid = entry->peer_link->EndpointUid();
              if (!uid.empty()) {
                aether_runtime->OpenPeer(uid);
              }
            }
          }

          if (!deferred_open_while_registering.empty()) {
            auto deferred = std::move(deferred_open_while_registering);
            deferred_open_while_registering.clear();
            for (auto& req : deferred) {
              resolve_and_open_peer(req);
            }
          }

          publication_dirty = true;
        });
      },
      /*on_failed=*/
      [this](std::string err) {
        EnqueueInternalModelWork([this, err = std::move(err)]() {
          AssertModelThread();
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
      [this, set_peer_presence](std::string peer_uid, PeerPresence presence) {
        EnqueueInternalModelWork([set_peer_presence, peer_uid = std::move(peer_uid),
                                  presence]() {
          set_peer_presence(peer_uid, presence);
        });
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

    if (stop_) {
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

    ShutdownEndpointAndDrain(aether_runtime.get());

    {
      std::lock_guard<std::mutex> lock{queue_mu_};
      accepting_internal_delivery_ = false;
    }

    DrainModelQueue();

    persist_workspace();

    sync_runtime.reset();
    transport.reset();
    workspace = {};

  } catch (std::exception const& ex) {
    worker_failed = true;
    worker_error = ex.what();
    UpdateStatus([&worker_error](ChatRuntimeStatus& s) {
      s.lifecycle_state = SessionLifecycleState::kFailed;
      s.error_text = worker_error;
    });
    ShutdownEndpointAndDrain(aether_runtime.get());
    {
      std::lock_guard<std::mutex> lock{queue_mu_};
      accepting_internal_delivery_ = false;
    }
    DrainModelQueue();
    sync_runtime.reset();
    transport.reset();
    workspace = {};
  } catch (...) {
    worker_failed = true;
    UpdateStatus([](ChatRuntimeStatus& s) {
      s.lifecycle_state = SessionLifecycleState::kFailed;
      s.error_text = "ChatSession worker failed with unknown error";
    });
    ShutdownEndpointAndDrain(aether_runtime.get());
    {
      std::lock_guard<std::mutex> lock{queue_mu_};
      accepting_internal_delivery_ = false;
    }
    DrainModelQueue();
    sync_runtime.reset();
    transport.reset();
    workspace = {};
  }

  finalize_worker();
}

}  // namespace apptraverse::example::chat_demo
