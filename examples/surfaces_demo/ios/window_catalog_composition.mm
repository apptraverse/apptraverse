#import "window_catalog_composition.h"

#include <filesystem>
#include <memory>
#include <set>
#include <utility>

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/platform/ios/ios_application_bridge.h"
#include "apptraverse/single_file_user_storage.h"
#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_prepare.h"
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
#include "window_catalog_distillation.h"
#else
#include "embedded_distilled_window_catalog_supply.h"
#endif

namespace {

struct CatalogProfile {
  explicit CatalogProfile(std::filesystem::path path)
      : directory{std::move(path)} {}

  std::filesystem::path directory;
  ae::RamDomainStorage backing;
  ae::RamDomainStorage user;
  std::set<ae::ObjId> constants;
  bool prepared = false;

  int Run(apptraverse::ApplicationModelRuntime& runtime,
          apptraverse::ApplicationPublishedCallback on_published) {
    if (!prepared) {
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
      // One fresh supply per Debug process. Foreground sessions reuse this
      // profile's RAM USER and BACKING instead of creating another graph.
      auto const run_directory = directory /
          ("debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id()));
      auto initial = apptraverse::DistillWindowCatalog(run_directory);
      directory = run_directory / "user";
      if (!apptraverse::LoadDirectoryDomainStorageIntoRam(
              initial.backing_directory, backing)) return 1;
      constants = std::move(initial.constant_ids);
#else
      auto const& graph = apptraverse::embedded_distilled::window_catalog::kEmbeddedGraph;
      apptraverse::InstallEmbeddedDistilledBacking(backing, graph);
      constants = apptraverse::EmbeddedSupplyConstantIdSet(graph);
      if (!apptraverse::LoadUserStorageFile(user, directory)) return 1;
#endif
      prepared = true;
    }
    apptraverse::OverlayDomainStorage storage{user, backing, constants};
    apptraverse::ApplicationModelSessionHooks<apptraverse::WindowCatalog> hooks;
    hooks.prepare = apptraverse::PrepareWindowCatalogForRun;
    hooks.before_incremental = [](apptraverse::WindowCatalog& catalog) {
      catalog.pending_focus_window = {};
      catalog.pending_mobile_active_window = {};
    };
    hooks.persist = [this](apptraverse::WindowCatalog& catalog) {
      apptraverse::SaveWindowCatalogPersistentSnapshot(catalog);
      return apptraverse::SaveUserStorageFile(user, directory) ? 0 : 1;
    };
    return apptraverse::RunApplicationModelSession(
        storage,
        ae::ObjId{apptraverse::window_catalog_demo::ToObjId(
            apptraverse::window_catalog_demo::ObjId::Catalog)},
        runtime, std::move(on_published), hooks);
  }
};

}  // namespace

@interface ATWindowCatalogIosApplication () {
  std::shared_ptr<CatalogProfile> _profile;
}
@end

@implementation ATWindowCatalogIosApplication

- (instancetype)initWithUserDirectory:(NSString*)directory {
  if ((self = [super init])) {
    _profile = std::make_shared<CatalogProfile>(directory.fileSystemRepresentation);
  }
  return self;
}

- (ATIosApplicationBridge*)makeBridge:
    (id<ATIosApplicationBridgeDelegate>)delegate {
  auto profile = _profile;
  apptraverse::ui::ios::IosApplicationBridgeCallbacks callbacks;
  callbacks.run_model = [profile](apptraverse::ApplicationModelRuntime& runtime,
                                 apptraverse::ApplicationPublishedCallback published) {
    return profile->Run(runtime, std::move(published));
  };
  callbacks.close_desktop_window =
      [](ae::Obj& root, ae::Obj& closed_window,
         apptraverse::ModelObjectProxy& proxy, std::function<void()> stop) {
        auto& catalog = static_cast<apptraverse::WindowCatalog&>(root);
        if (catalog.persisted.main_window.id() == closed_window.obj_id) {
          stop();
          return true;
        }
        proxy.Invoke(catalog.obj_id, &apptraverse::WindowCatalog::UserCloseWindow,
                     closed_window.obj_id);
        return false;
      };
  callbacks.mobile_presentation = [](ae::Obj& root) {
    auto& catalog = static_cast<apptraverse::WindowCatalog&>(root);
    apptraverse::ui::ios::IosMobilePresentation result;
    for (auto const& window : catalog.BuildMobileRing())
      result.windows.push_back(window.id());
    result.active_window = catalog.ResolveMobileActiveWindow().id();
    return result;
  };
  callbacks.on_viewport = [](ae::Obj& root, apptraverse::ModelObjectProxy& proxy,
                             std::int32_t width, std::int32_t height) {
    auto& catalog = static_cast<apptraverse::WindowCatalog&>(root);
    auto const& viewport = catalog.persisted.main_client_viewport;
    if (viewport.width != width || viewport.height != height ||
        viewport.density_dpi != 160) {
      proxy.Invoke(catalog.obj_id,
          &apptraverse::WindowCatalog::SubmitMainClientViewportFromUi,
          width, height, std::int32_t{160});
    }
    for (auto const& window : catalog.BuildMobileRing()) {
      if (window.id() == catalog.persisted.main_window.id()) continue;
      if (window->client_width() == width && window->client_height() == height)
        continue;
      proxy.Invoke(catalog.obj_id,
          &apptraverse::WindowCatalog::SubmitHolderChildWindowClientSizeFromUi,
          window.id().id(), width, height);
    }
  };
  callbacks.on_mobile_swipe = [](ae::Obj& root,
      apptraverse::ModelObjectProxy& proxy, int delta) {
    proxy.Invoke(root.obj_id, &apptraverse::WindowCatalog::SubmitMobileSwipeFromUi,
                 delta);
  };
  callbacks.on_mobile_page = [](ae::Obj& root,
      apptraverse::ModelObjectProxy& proxy, ae::ObjId window) {
    proxy.Invoke(root.obj_id, &apptraverse::WindowCatalog::SubmitMobileActiveFromUi,
                 window);
  };
  return apptraverse::ui::ios::MakeIosApplicationBridge(std::move(callbacks), delegate);
}
@end

void ATRegisterWindowCatalogIosApplication(void) {
  apptraverse::EnsureWindowCatalogGraphRegistration();
}
