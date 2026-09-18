#include "messenger_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>

#include "messenger_distill.h"
#endif

#include <cassert>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync_runtime.h"

#include "aether_byte_transport.h"
#include "aether_link.h"
#include "chat_aether_runtime.h"
#include "messenger_ids.h"
#include "messenger_model.h"

namespace apptraverse {
namespace {

void LogMessengerLifecycle(char const* stage, std::string const& detail) {
  std::fprintf(stderr, "[messenger] %s %s\n", stage, detail.c_str());
  std::fflush(stderr);
}

}  // namespace

void WireMessengerSyncStack(
    Application& application, ae::Domain& domain, ae::IDomainStorage& storage,
    example::chat_demo::IAetherFrameEndpoint& aether,
    example::chat_demo::ModelDispatch dispatch,
    std::unique_ptr<example::chat_demo::AetherByteTransport>& transport,
    std::unique_ptr<SharedSyncRuntime>& sync_runtime) {
  if (transport || !application.aether_ready ||
      application.local_endpoint_uid.empty()) {
    return;
  }

  transport = std::make_unique<example::chat_demo::AetherByteTransport>(
      aether, application.local_endpoint_uid, std::move(dispatch));
  sync_runtime =
      std::make_unique<SharedSyncRuntime>(domain, storage, *transport);
  sync_runtime->AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  Application* const app_ptr = &application;
  sync_runtime->SetInitialNodeImportedCallback(
      [app_ptr](std::string const& source_endpoint,
                SharedNode::ptr imported_node) -> bool {
        if (!imported_node.is_valid()) {
          return false;
        }
        Dialog& dialog = *app_ptr->surfaces->surfaces.front()->dialog;
        if (source_endpoint != dialog.peer_uid) {
          LogMessengerLifecycle(
              "import_reject",
              "source=" + source_endpoint + " peer=" + dialog.peer_uid);
          return false;
        }
        if (ae::Registry::GetRegistry().GenerationDistance(
                Conversation::kClassId, imported_node->GetClassId()) < 0) {
          LogMessengerLifecycle("import_reject", "class_mismatch");
          return false;
        }
        auto conversation = Conversation::ptr::MakeFromThis(
            static_cast<Conversation*>(&*imported_node));
        conversation->SetJournalCompactionBlocked(true);
        conversation->CopyMaterializedChangeNotifierFrom(dialog);
        dialog.BindConversation(conversation);
        app_ptr->peer_prepared_for_sync = true;
        LogMessengerLifecycle(
            "import_bind",
            "conv=" + std::to_string(conversation.id().id()) +
                " source=" + source_endpoint);
        return true;
      });

  application.sync_runtime = sync_runtime.get();
  LogMessengerLifecycle("sync_stack",
                        "uid=" + application.local_endpoint_uid);
}

namespace {

void EnsureSyncStack(MessengerModelSession& session, Application& application,
                     ae::Domain& domain, ae::IDomainStorage& storage) {
  if (session.transport || !application.aether_ready ||
      application.local_endpoint_uid.empty() || !session.aether) {
    return;
  }

  example::chat_demo::ModelDispatch dispatch =
      [&session](example::chat_demo::ModelTask task) {
        session.Post([task = std::move(task)](ae::Domain&) { task(); });
      };

  WireMessengerSyncStack(application, domain, storage, *session.aether,
                         std::move(dispatch), session.transport,
                         session.sync_runtime);
}

}  // namespace

void MessengerModelSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu};
    stop = true;
  }
  cv.notify_all();
  if (aether) {
    aether->RequestStop();
  }
}

void MessengerModelSession::Post(ModelWork work) {
  {
    std::lock_guard<std::mutex> lock{mu};
    if (stop) {
      return;
    }
    pending_work.push_back(std::move(work));
  }
  cv.notify_all();
}

void ApplyMessengerStructural(std::vector<std::uint8_t> const& bytes,
                              Application& ui_application,
                              ae::IDomainStorage& ui_storage, void* host,
                              ModelObjectProxy* model_proxy) {
  auto surfaces = ui_application.surfaces;
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  ApplyStructuralPublicationAndUpdatePresenters(
      in, *ui_application.domain, ui_storage, ui_application, host,
      model_proxy);

  for (auto const& surface : surfaces->surfaces) {
    surface->presenter->OnModelChanged();
  }
}

void MessengerModelSession::Run(
    std::function<void(MessengerPublicationKind)> on_published) {
  example::chat_demo::EnsureAetherLinkRegistration();

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  bool state_missing = true;
  if (std::filesystem::exists(state_dir)) {
    DirectoryDomainStorage probe{state_dir};
    state_missing =
        probe
            .Enumerate(ae::ObjId{messenger::ToObjId(
                messenger::ObjId::Application)})
            .empty();
  }
  if (state_missing) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildMessengerGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }
#endif

  {
    DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{messenger::ToObjId(
                    messenger::ObjId::Application)});

    PendingDirtyNodes pending_dirty;
    BindReachableNodesMaterializedChangeNotifier(
        *application, &pending_dirty, &PendingDirtyNodesNotify);

    auto* buffer = channel.AcquireProducer();
    SerializeInitialPublication(*application, buffer->sink);
    {
      std::lock_guard<std::mutex> lock{mu};
      channel.NotePublished();
      channel.PublishProducer();
    }
    cv.notify_all();
    on_published(MessengerPublicationKind::Initial);

    Application* const app_ptr = &*application;
    aether = std::make_unique<example::chat_demo::ChatAetherRuntime>();
    app_ptr->aether = aether.get();
    app_ptr->aether_ready = false;
    app_ptr->sync_runtime = nullptr;

    example::chat_demo::IAetherFrameEndpoint::Config aether_cfg;
    aether_cfg.state_dir = state_dir / "aether";
    aether_cfg.client_name = "apptraverse-messenger";
    aether_cfg.heartbeat_period_ms = 1000;
    aether_cfg.offline_after_ms = 4000;

    aether->Start(
        std::move(aether_cfg),
        [this, app_ptr, &domain, &storage](std::string uid) {
          Post([this, app_ptr, &domain, &storage,
                uid = std::move(uid)](ae::Domain&) {
            app_ptr->OnAetherLocalUid(std::move(uid));
            EnsureSyncStack(*this, *app_ptr, domain, storage);
            app_ptr->SetupActivePeerSync();
          });
        },
        [this, app_ptr, &domain, &storage]() {
          Post([this, app_ptr, &domain, &storage](ae::Domain&) {
            app_ptr->OnAetherReady();
            EnsureSyncStack(*this, *app_ptr, domain, storage);
            app_ptr->SetupActivePeerSync();
          });
        },
        [this](std::string error) {
          LogMessengerLifecycle("aether_error", error);
          Post([error = std::move(error)](ae::Domain&) {
            LogMessengerLifecycle("aether_error_model", error);
          });
        },
        {}, {}, {},
        [this, app_ptr](std::string source_uid,
                        std::vector<std::uint8_t> bytes) {
          Post([app_ptr, source_uid = std::move(source_uid),
                bytes = std::move(bytes)](ae::Domain&) {
            app_ptr->OnControlMessage(std::move(source_uid), std::move(bytes));
          });
        });

    for (;;) {
      std::optional<ModelWork> work;
      bool draining = false;
      {
        std::unique_lock<std::mutex> lock{mu};
        bool const sync_active = app_ptr->NeedsPeriodicSyncWake();
        if (sync_active) {
          cv.wait_for(lock, std::chrono::milliseconds(250), [&] {
            if (stop) {
              return true;
            }
            if (!pending_work.empty()) {
              return true;
            }
            return !pending_dirty.empty() && !channel.is_publication_busy();
          });
        } else {
          cv.wait(lock, [&] {
            if (stop) {
              return true;
            }
            if (!pending_work.empty()) {
              return true;
            }
            return !pending_dirty.empty() && !channel.is_publication_busy();
          });
        }
        if (stop) {
          if (pending_work.empty()) {
            break;
          }
          work = std::move(pending_work.front());
          pending_work.pop_front();
          draining = true;
        } else if (!pending_work.empty()) {
          work = std::move(pending_work.front());
          pending_work.pop_front();
        }
      }

      if (work) {
        (*work)(domain);
      }

      if (draining) {
        continue;
      }

      for (;;) {
        std::uint32_t pending_id = 0;
        {
          std::lock_guard<std::mutex> lock{mu};
          if (stop || channel.is_publication_busy() || pending_dirty.empty()) {
            break;
          }
          pending_id = pending_dirty.PopFront();
        }
        Node* const to_publish = FindLiveReachableNode(*application,
                                                       pending_id);
        if (to_publish == nullptr) {
          continue;
        }
        auto* pub = channel.AcquireProducer();
        SerializeStructuralNodePublication(*to_publish, pub->sink);
        {
          std::lock_guard<std::mutex> lock{mu};
          channel.NotePublished();
          channel.PublishProducer();
        }
        cv.notify_all();
        on_published(MessengerPublicationKind::Incremental);
      }

      if (sync_runtime) {
        app_ptr->DriveConversationSync();
      }
    }

    application->aether = nullptr;
    application->sync_runtime = nullptr;
    application->aether_ready = false;
    sync_runtime.reset();
    transport.reset();
    if (aether) {
      aether->RequestStop();
      aether->Join();
      aether.reset();
    }

    ClearReachableNodesMaterializedChangeNotifier(*application);
    application.Save();
  }
}

}  // namespace apptraverse
