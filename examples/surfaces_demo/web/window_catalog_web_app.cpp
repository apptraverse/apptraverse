#include "window_catalog_web_app.h"

#include <utility>

#include <emscripten.h>
#include <emscripten/threading.h>

#include "window_catalog_graph_web.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

void MainThreadPublication(int kind_flag) {
  WebWindowCatalogApp::Instance().OnPublicationFromModel(kind_flag);
}

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

WebWindowCatalogApp& WebWindowCatalogApp::Instance() {
  static WebWindowCatalogApp app;
  return app;
}

void WebWindowCatalogApp::Start(std::filesystem::path state_dir) {
  EnsureObjectRegistration();
  EnsureWebWindowCatalogGraphRegistration();

  if (!emscripten_is_main_browser_thread()) {
    EM_ASM({
      throw new Error('WebWindowCatalogApp::Start must run on the browser main thread');
    });
  }
  if (!EM_ASM_INT({ return crossOriginIsolated ? 1 : 0; })) {
    EM_ASM({
      document.getElementById('status').textContent =
          'FAIL: crossOriginIsolated is false (need COOP/COEP headers)';
      throw new Error('crossOriginIsolated is false');
    });
  }
  if (!EM_ASM_INT({
        return (typeof SharedArrayBuffer !== 'undefined') ? 1 : 0;
      })) {
    EM_ASM({
      document.getElementById('status').textContent =
          'FAIL: SharedArrayBuffer unavailable';
      throw new Error('SharedArrayBuffer unavailable');
    });
  }

  EM_ASM({ document.getElementById('status').textContent = 'Loading…'; });

  state_dir_ = std::move(state_dir);
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    runtime_.Post(std::move(work));
  });

  model_thread_ = std::thread([this] {
    DirectoryDomainStorage storage{state_dir_};
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
    std::error_code profile_error;
    bool const profile_exists =
        std::filesystem::exists(state_dir_, profile_error);
    if (profile_error) {
      WriteFatalStderr("fatal: cannot inspect catalog profile\n");
      std::abort();
    }
    bool const profile_absent = !profile_exists ||
                               std::filesystem::is_empty(state_dir_, profile_error);
    if (profile_error) {
      WriteFatalStderr("fatal: cannot inspect catalog profile contents\n");
      std::abort();
    }
    if (profile_absent) {
      DirectoryDomainStorage bootstrap_storage{state_dir_};
      ae::Domain bootstrap_domain{bootstrap_storage};
      auto bootstrap_catalog = BuildWebWindowCatalogGraph(bootstrap_domain);
      FinalizeDistilledGraph(*bootstrap_catalog);
      SaveDistilledRoot(*bootstrap_catalog);
    }
#endif

    ApplicationModelSessionHooks<WindowCatalog> hooks;
    hooks.prepare = PrepareWindowCatalogForRun;
    hooks.persist = [](WindowCatalog&) -> int { return 0; };

    (void)RunApplicationModelSession(
        storage, CatalogRootId(), runtime_,
        [this](ApplicationPublicationKind kind) {
          std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopy();
          runtime_.NotifyPublicationConsumed();
          int const kind_flag =
              kind == ApplicationPublicationKind::Initial ? 0 : 1;
          {
            std::lock_guard<std::mutex> lock{publication_mu_};
            pending_publication_bytes_ = std::move(bytes);
            pending_publication_kind_ = kind_flag;
          }
          emscripten_async_run_in_main_runtime_thread(
              EM_FUNC_SIG_VI, &MainThreadPublication, kind_flag);
        },
        hooks);
  });
}

void WebWindowCatalogApp::OnPublicationFromModel(int kind_flag) {
  if (kind_flag != 0) {
    return;
  }
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{publication_mu_};
    bytes = std::move(pending_publication_bytes_);
    pending_publication_kind_ = -1;
  }
  if (bytes.empty()) {
    EM_ASM({
      document.getElementById('status').textContent =
          'FAIL: empty initial publication';
    });
    return;
  }
  if (!ui_session_) {
    ui_session_ = std::make_unique<ApplicationUiSession>(
        bytes, this, *model_proxy_);
    EM_ASM({
      document.getElementById('status').textContent = '';
      console.log('WINDOW_CATALOG_INITIAL_RENDER_OK');
    });
  }
}

}  // namespace apptraverse
