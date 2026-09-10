#include "web_app.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <emscripten.h>
#include <emscripten/threading.h>

#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"

#include "surfaces_ids.h"
#include "web_surface_presenter.h"

namespace apptraverse {
namespace {

void MainThreadPublication(int kind) {
  WebApp::Instance().OnPublicationFromModel(kind);
}

}  // namespace

WebApp& WebApp::Instance() {
  static WebApp app;
  return app;
}

void WebApp::Start(std::filesystem::path state_dir) {
  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureWebSurfacePresenterRegistration();

  if (!emscripten_is_main_browser_thread()) {
    EM_ASM({
      throw new Error('WebApp::Start must run on the browser main thread');
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

  session_.state_dir = std::move(state_dir);
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });

  model_thread_ = std::thread([this] {
    session_.Run([](SurfacesPublicationKind kind) {
      int const flag = kind == SurfacesPublicationKind::Initial ? 0 : 1;
      // Model pthread must not touch the DOM.
      emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI,
                                                  &MainThreadPublication, flag);
    });
    // Save already happened inside Run. Finish UI teardown + IDBFS sync on main.
    emscripten_async_run_in_main_runtime_thread(
        EM_FUNC_SIG_V, +[]() { WebApp::Instance().FinishStopAndSync(); });
  });
}

void WebApp::OnPublicationFromModel(int kind) {
  ConsumePublication(kind == 0 ? SurfacesPublicationKind::Initial
                               : SurfacesPublicationKind::Incremental);
}

void WebApp::ConsumePublication(SurfacesPublicationKind kind) {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();

  if (kind == SurfacesPublicationKind::Initial) {
    ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
    ByteSource in;
    in.data = bytes.data();
    in.size = bytes.size();
    auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
    ui_application_ = Application::ptr::MakeFromThis(
        static_cast<Application*>(ui_root.get()));
    initializing_presentation_ = true;
    InitializePresenters(*ui_application_, this, &*model_proxy_);
    initializing_presentation_ = false;
    EnsureModelCurrentSeeded();
    EM_ASM({ document.getElementById('status').textContent = ""; });
  } else {
    structural_apply_in_progress_ = true;
    ApplySurfacesStructural(bytes, *ui_application_, ui_storage_, this,
                            &*model_proxy_);
    structural_apply_in_progress_ = false;
  }
  QueueIndexedDbPersist();
  SyncTabOrder();
  ShowCurrentPage();
}

void WebApp::QueueIndexedDbPersist() {
  if (stopping_ || stopped_) {
    return;
  }
  // Model Domain files are written only on Application::Save. Browser reload
  // skips RequestStop, so the web host saves after each publication, then
  // syncfs(false) to IndexedDB. SurfacesModelSession itself is unchanged.
  session_.Post([](ae::Domain& domain) {
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    application.Save();
    emscripten_async_run_in_main_runtime_thread(
        EM_FUNC_SIG_V, +[]() { WebApp::Instance().SyncIndexedDbFromFs(); });
  });
}

void WebApp::SyncIndexedDbFromFs() {
  if (stopped_) {
    return;
  }
  // JS queue lives on Module so overlapping syncfs(false) calls serialize.
  EM_ASM({
    var state = Module.apptraverseSync || (Module.apptraverseSync = {busy: 0, again: 0});
    if (state.busy) {
      state.again = 1;
      return;
    }
    state.busy = 1;
    var finish = function(err) {
      state.busy = 0;
      if (err) {
        console.error('IDBFS syncfs(false) failed', err);
      } else {
        console.log('SURFACES_IDBFS_SYNCED');
      }
      if (state.again) {
        state.again = 0;
        Module._AppTraverseWebRequestSync();
      }
    };
    FS.syncfs(false, finish);
  });
}

void WebApp::OnIndexedDbSyncDone(int /*err*/) {}

void WebApp::RequestSyncFromJs() { SyncIndexedDbFromFs(); }

void WebApp::SyncTabOrder() {
  auto const& surfaces = ui_application_->surfaces->surfaces;
  std::string joined;
  for (auto const& surface : surfaces) {
    if (!joined.empty()) {
      joined += ',';
    }
    joined += std::to_string(surface->obj_id.id());
  }
  EM_ASM(
      {
        var tabs = document.getElementById('tab-strip');
        var order = UTF8ToString($0).split(',').filter(Boolean);
        for (var i = 0; i < order.length; ++i) {
          var button =
              tabs.querySelector('[data-surface-id="' + order[i] + '"]');
          if (button) {
            tabs.appendChild(button);
          }
        }
      },
      joined.c_str());
}

std::uint32_t WebApp::ModelCurrentId() const {
  auto const& current = ui_application_->surfaces->mobile_current;
  if (!current) {
    return 0;
  }
  return current->obj_id.id();
}

void WebApp::ShowCurrentPage() {
  auto const& surfaces = ui_application_->surfaces->surfaces;
  std::uint32_t current_id = ModelCurrentId();
  // Display fallback until a seed / settle PageShown publication arrives.
  if (current_id == 0 && !surfaces.empty()) {
    current_id = surfaces[0]->obj_id.id();
  }
  if (current_id == 0) {
    EM_ASM({
      var tabs = document.querySelectorAll('.surface-tab');
      for (var i = 0; i < tabs.length; ++i) {
        tabs[i].classList.remove('active');
      }
      document.getElementById('content').textContent = "";
    });
    return;
  }
  std::uint32_t number = 0;
  for (auto const& surface : surfaces) {
    if (surface->obj_id.id() == current_id) {
      number = surface->number;
      break;
    }
  }
  EM_ASM(
      {
        var currentId = String(($0) >>> 0);
        var tabs = document.querySelectorAll('.surface-tab');
        for (var i = 0; i < tabs.length; ++i) {
          tabs[i].classList.toggle('active',
                                   tabs[i].dataset.surfaceId === currentId);
        }
        document.getElementById('content').textContent = 'Surface ' + $1;
      },
      current_id, number);
}

void WebApp::EnsureModelCurrentSeeded() {
  if (ui_application_->surfaces->mobile_current) {
    return;
  }
  auto const& surfaces = ui_application_->surfaces->surfaces;
  if (surfaces.empty()) {
    return;
  }
  auto presenter = FindLivePresenter(surfaces[0]->obj_id.id());
  if (!presenter) {
    return;
  }
  presenter->PageShown();
}

SurfacePresenter::ptr WebApp::FindLivePresenter(std::uint32_t surface_id) {
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    if (surface->obj_id.id() == surface_id) {
      return SurfacePresenter::ptr{surface->presenter};
    }
  }
  return {};
}

void WebApp::OnSurfacePageLoaded(std::uint32_t surface_id, std::uint32_t,
                                 bool /*newly_created*/) {
  if (initializing_presentation_) {
    return;
  }
  // New page from Add: settle like the Android pager on the new item.
  if (structural_apply_in_progress_) {
    auto presenter = FindLivePresenter(surface_id);
    if (presenter) {
      presenter->PageShown();
    }
  }
}

void WebApp::OnSurfacePageUnloaded(std::uint32_t surface_id) {
  // Model already cleared mobile_current when the removed Surface was current.
  // Host policy: settle on the neighbor at pending_remove_index_.
  if (ModelCurrentId() != 0 && ModelCurrentId() != surface_id) {
    return;
  }
  auto const& surfaces = ui_application_->surfaces->surfaces;
  if (surfaces.empty()) {
    return;
  }
  std::size_t const index =
      std::min(pending_remove_index_, surfaces.size() - 1);
  auto presenter = FindLivePresenter(surfaces[index]->obj_id.id());
  if (presenter) {
    presenter->PageShown();
  }
}

void WebApp::SelectSurface(std::uint32_t surface_id) {
  if (stopped_ || !ui_application_) {
    return;
  }
  auto presenter = FindLivePresenter(surface_id);
  if (!presenter) {
    return;
  }
  presenter->PageShown();
}

void WebApp::AddCurrent() {
  if (stopped_) {
    return;
  }
  std::uint32_t current_id = ModelCurrentId();
  if (current_id == 0 && !ui_application_->surfaces->surfaces.empty()) {
    current_id = ui_application_->surfaces->surfaces[0]->obj_id.id();
  }
  auto presenter = FindLivePresenter(current_id);
  if (!presenter) {
    return;
  }
  presenter->AddClick();
}

void WebApp::RemoveCurrent() {
  if (stopped_) {
    return;
  }
  auto& surfaces = ui_application_->surfaces->surfaces;
  std::uint32_t current_id = ModelCurrentId();
  if (current_id == 0 && !surfaces.empty()) {
    current_id = surfaces[0]->obj_id.id();
  }
  if (surfaces.size() == 1) {
    RequestStop();
    return;
  }
  pending_remove_index_ = 0;
  for (std::size_t i = 0; i < surfaces.size(); ++i) {
    if (surfaces[i]->obj_id.id() == current_id) {
      pending_remove_index_ = i;
      break;
    }
  }
  auto presenter = FindLivePresenter(current_id);
  if (!presenter) {
    return;
  }
  presenter->RemoveClick();
}

void WebApp::RequestStop() {
  if (stopping_ || stopped_) {
    return;
  }
  stopping_ = true;
  EM_ASM({ document.getElementById('status').textContent = 'Stopping…'; });
  session_.RequestStop();
}

void WebApp::FinishStopAndSync() {
  if (stopped_) {
    return;
  }
  if (model_thread_.joinable()) {
    model_thread_.join();
  }
  if (ui_application_) {
    UnloadPresenters(*ui_application_);
    ui_application_ = {};
  }
  ui_domain_.reset();
  model_proxy_.reset();
  stopped_ = true;

  EM_ASM({
    document.getElementById('toolbar').style.display = 'none';
    document.getElementById('tab-strip').innerHTML = "";
    document.getElementById('content').textContent =
        'Stopped — reload to start again';
    document.getElementById('status').textContent = 'Saving to IndexedDB…';
    FS.syncfs(false, function(err) {
      if (err) {
        document.getElementById('status').textContent =
            'IndexedDB sync failed: ' + err;
        console.error(err);
        return;
      }
      document.getElementById('status').textContent = 'Saved.';
      console.log('SURFACES_IDBFS_SYNCED');
    });
  });
}

}  // namespace apptraverse
