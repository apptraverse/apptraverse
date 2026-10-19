#include "surfaces_lifecycle.h"

#include "surfaces_shutdown_trace.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include "surfaces_distill.h"
#endif

#include <optional>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/distill.h"
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
  session.config.on_model_root_loaded = [](ae::Obj& root) {
    auto& application = static_cast<Application&>(root);
    assert(application.surfaces.is_valid() &&
           application.surfaces.is_loaded());
    application.surfaces->host_application = &application;
    WireSurfacesDemoPlatformBindings(*application.surfaces);
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
  session.config.on_save_start = [] {
    SurfacesShutdownTrace::MarkApplicationSaveStart();
  };
  session.config.persist_root = [](ae::Obj& root) {
    Application::ptr::MakeFromThis(static_cast<Application*>(&root)).Save();
  };
  session.config.on_save_end = [](ae::Obj&, DirectoryDomainStorage& durable) {
    SurfacesShutdownTrace::MarkApplicationSaveEnd();
    DirectoryDomainStorageStoreStats const store_stats =
        durable.SnapshotSaveSessionStats();
    SurfacesShutdownTrace::LogStorageStoreStats(
        store_stats.layer_store_calls, store_stats.layers_bytes_accepted,
        store_stats.layers_written, store_stats.layers_skipped_unchanged,
        store_stats.layers_written_bytes, store_stats.layers_failed_commits);
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
