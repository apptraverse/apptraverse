#include "surfaces_lifecycle.h"

#include "surfaces_shutdown_trace.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include "surfaces_distill.h"
#endif

#include <chrono>
#include <optional>

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/graph_walk.h"
#include "apptraverse/object_serialization.h"

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"

#include "surfaces_demo_platform_attach.h"

namespace apptraverse {
namespace {

thread_local std::optional<InstrumentedDomainStorage> g_instrumented_session_storage;

}  // namespace

void ConfigureSurfacesDemoModelSession(SurfacesModelSession& session) {
  using surfaces_demo::ObjId;
  using surfaces_demo::ToObjId;

  session.config.root_object_id =
      ae::ObjId{ToObjId(ObjId::Application)};
  session.config.ensure_type_registration = EnsureSurfacesRegistration;
  session.config.load_mutable_root = [&session](ae::Domain& domain) -> ae::Obj* {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{ToObjId(ObjId::Application)});
    session.model_root = application;
    return &*application;
  };
  session.config.create_checkpoint_root_shell =
      [&session](ae::Domain& domain) -> ae::Obj* {
        auto application = Application::ptr::Create(ae::CreateWith{domain}.with_id(
            ae::ObjId{ToObjId(ObjId::Application)}));
        session.model_root = application;
        return &*application;
      };
  session.config.on_model_root_loaded = [](ae::Obj& root) {
    auto& application = static_cast<Application&>(root);
    Surfaces* surfaces = nullptr;
    if (application.surfaces.is_valid()) {
      if (!application.surfaces.is_loaded()) {
        (void)application.surfaces.Load();
      }
      if (application.surfaces.is_loaded()) {
        surfaces = &*application.surfaces;
      }
    }
    if (surfaces == nullptr) {
      std::vector<ae::Obj*> objects;
      CollectReachableObjects(root, objects);
      for (ae::Obj* obj : objects) {
        if (ae::Registry::GetRegistry().GenerationDistance(
                Surfaces::kClassId, obj->GetClassId()) >= 0) {
          surfaces = static_cast<Surfaces*>(obj);
          break;
        }
      }
    }
    if (surfaces == nullptr) {
      return;
    }
    surfaces->host_application = &application;
    WireSurfacesDemoPlatformBindings(*surfaces);
  };
  session.config.bind_presentation_size_queue =
      [](PresentationSizeQueue& queue) {
        queue.BindSizeApplier([](ae::Domain& domain, ae::ObjId surface_id,
                                 std::int32_t width, std::int32_t height) {
          auto object = domain.Find(surface_id);
          if (!object) {
            return;
          }
          if (Surface* surface = object.as<Surface>()) {
            surface->SetPresentationSize(width, height);
          }
        });
      };
#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  session.config.bootstrap_if_missing =
      [](ae::Domain& domain, ae::IDomainStorage& storage) {
        (void)storage;
        auto bootstrap_app = BuildSurfacesGraph(domain);
        FinalizeDistilledGraph(*bootstrap_app);
        SaveDistilledRoot(*bootstrap_app);
      };
#endif

  session.config.select_storage = [](DirectoryDomainStorage& durable)
      -> ae::IDomainStorage* {
    if (!SurfacesShutdownTrace::Enabled()) {
      return &durable;
    }
    g_instrumented_session_storage.emplace(durable, durable.root(), &durable);
    return &*g_instrumented_session_storage;
  };
  session.config.on_model_drained_accepted_tasks = [] {
    SurfacesShutdownTrace::MarkModelDrainedAcceptedTasks();
  };
  session.config.on_clear_live_notifiers = [](ae::Obj&) {
    SurfacesShutdownTrace::MarkClearLiveNotifiersDone();
  };
  session.config.durable_persistence =
      ModelDurablePersistence::PackedCheckpointFile;
  session.config.on_save_start = [] {
    SurfacesShutdownTrace::MarkApplicationSaveStart();
  };
  session.config.on_save_end = [](ae::Obj&,
                                  ModelPublicationSaveContext const& save) {
    SurfacesShutdownTrace::MarkApplicationSaveEnd();
    if (save.backend == ModelDurablePersistence::DirectoryObjectLayers &&
        save.directory_storage != nullptr) {
      DirectoryDomainStorageStoreStats const store_stats =
          save.directory_storage->SnapshotSaveSessionStats();
      SurfacesShutdownTrace::LogStorageStoreStats(
          store_stats.layer_store_calls, store_stats.layers_bytes_accepted,
          store_stats.layers_written, store_stats.layers_skipped_unchanged,
          store_stats.layers_written_bytes, store_stats.layers_failed_commits,
          store_stats.layers_compare_bytes_read,
          store_stats.layers_size_mismatch_writes,
          store_stats.layers_new_file_writes);
    } else if (save.backend == ModelDurablePersistence::PackedCheckpointFile) {
      SurfacesShutdownTrace::LogCheckpointCommitStats(save.success,
                                                      save.checkpoint_bytes);
    }
    SurfacesShutdownTrace::MarkApplicationGraphTeardownStart();
  };
}

void RunSurfacesDemoModelSession(
    SurfacesModelSession& session,
    std::function<void(SurfacesPublicationKind)> on_published) {
  g_instrumented_session_storage.reset();
  ConfigureSurfacesDemoModelSession(session);
  session.Run(std::move(on_published));
  g_instrumented_session_storage.reset();
  SurfacesShutdownTrace::MarkModelDomainDestroyed();
}

void ApplySurfacesStructural(std::vector<std::uint8_t> const& bytes,
                             Application& ui_application,
                             ae::IDomainStorage& ui_storage, void* host,
                             ModelObjectProxy* model_proxy) {
  ApplyModelStructuralPublication(bytes, ui_application, ui_storage, host,
                                  model_proxy);
}

}  // namespace apptraverse
