#include <atomic>
#include <cstdlib>
#include <deque>
#include <memory>
#include <vector>

#include <emscripten.h>
#include <emscripten/em_js.h>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include "minimal_app_controls.h"
#include "minimal_app_prepare.h"
#include "minimal_app_web_graph.h"
#include "minimal_app_window_events.h"
#include "minimal_application.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      EM_ASM({ throw new Error('CHECK failed: ' + UTF8ToString($0)); }, #cond); \
      std::abort();                                                          \
    }                                                                        \
  } while (0)

using example::minimal::BuildWebMinimalApplicationGraph;
using example::minimal::EnsureMinimalAppControlsRegistration;
using example::minimal::EnsureMinimalApplicationRegistration;
using example::minimal::MinimalApplication;
using example::minimal::MinimalCloseAuxiliaryWindowEvent;
using example::minimal::MinimalOpenAuxiliaryWindowEvent;

EM_JS(int, ApptraverseCountEmvalHandles, (), {
  return Module.count_emval_handles();
});

EM_JS(emscripten::EM_VAL, ApptraverseGetAppMountElement, (), {
  return Emval.toHandle(document.getElementById('app-mount'));
});

EM_JS(void, ApptraverseLifecycleCaptureMainDomRefs, (), {
  var mount = document.getElementById('app-mount');
  var wins = mount.querySelectorAll('[data-apptraverse-desktop-window="1"]');
  if (wins.length < 1) {
    throw new Error('lifecycle capture: missing main desktop window');
  }
  var win = wins[0];
  var list = mount.querySelector('[data-apptraverse-control-id="7102"]');
  var listRow = list ? list.querySelector('.apptraverse-list-row') : null;
  globalThis.__apptraverseLifecycleMainCapture = {
    client: win.firstChild,
    button: mount.querySelector('[data-apptraverse-control-id="7101"]'),
    list: list,
    listRow: listRow
  };
  var cap = globalThis.__apptraverseLifecycleMainCapture;
  if (!cap.button || !cap.list || !cap.listRow) {
    throw new Error('lifecycle capture: missing main button, list, or row');
  }
});

EM_JS(void, ApptraverseLifecycleCaptureAuxDomRefs, (), {
  var mount = document.getElementById('app-mount');
  var list = mount.querySelector('[data-apptraverse-control-id="7202"]');
  var listRow = list ? list.querySelector('.apptraverse-list-row') : null;
  globalThis.__apptraverseLifecycleAuxCapture = {
    button: mount.querySelector('[data-apptraverse-control-id="7201"]'),
    list: list,
    listRow: listRow,
    closer: mount.querySelector('[data-apptraverse-control-id="7203"]')
  };
  var cap = globalThis.__apptraverseLifecycleAuxCapture;
  if (!cap.button || !cap.list || !cap.listRow || !cap.closer) {
    throw new Error('lifecycle capture: missing auxiliary controls');
  }
});

EM_JS(int, ApptraverseLifecycleCountDesktopWindows, (), {
  return document.getElementById('app-mount')
      .querySelectorAll('[data-apptraverse-desktop-window="1"]').length;
});

EM_JS(int, ApptraverseLifecycleCountMainButtons, (), {
  return document.getElementById('app-mount')
      .querySelectorAll('[data-apptraverse-control-id="7101"]').length;
});

EM_JS(int, ApptraverseLifecycleCountAuxButtons, (), {
  return document.getElementById('app-mount')
      .querySelectorAll('[data-apptraverse-control-id="7201"]').length;
});

EM_JS(void, ApptraverseLifecycleDispatchStaleMainControlEvents, (), {
  var cap = globalThis.__apptraverseLifecycleMainCapture;
  if (cap && cap.button) {
    cap.button.dispatchEvent(new MouseEvent('click', { bubbles: true }));
  }
  if (cap && cap.listRow) {
    cap.listRow.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    cap.listRow.dispatchEvent(new MouseEvent('dblclick', { bubbles: true }));
  }
});

EM_JS(void, ApptraverseLifecycleDispatchStaleAuxControlEvents, (), {
  var cap = globalThis.__apptraverseLifecycleAuxCapture;
  if (cap && cap.button) {
    cap.button.dispatchEvent(new MouseEvent('click', { bubbles: true }));
  }
  if (cap && cap.listRow) {
    cap.listRow.dispatchEvent(new MouseEvent('click', { bubbles: true }));
    cap.listRow.dispatchEvent(new MouseEvent('dblclick', { bubbles: true }));
  }
});

EM_JS(void, ApptraverseLifecycleClickMainApply, (), {
  var btn = document.querySelector('[data-apptraverse-control-id="7101"]');
  if (!btn) {
    throw new Error('lifecycle: missing main apply');
  }
  btn.dispatchEvent(new MouseEvent('click', { bubbles: true }));
});

EM_JS(void, ApptraverseLifecycleAssertDetachedClientClean, (), {
  var cap = globalThis.__apptraverseLifecycleMainCapture;
  if (!cap || !cap.client) {
    throw new Error('lifecycle: missing captured main client');
  }
  if (cap.client.querySelector('.apptraverse-push-button')) {
    throw new Error('lifecycle: main client retained push-button node');
  }
  if (cap.client.querySelector('.apptraverse-list')) {
    throw new Error('lifecycle: main client retained list node');
  }
});

std::vector<std::uint8_t> BuildInitialPublicationBytes() {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto application = BuildWebMinimalApplicationGraph(model_domain);
  PrepareMinimalApplicationForRun(*application);
  ByteSink initial;
  SerializeInitialPublication(*application, initial);
  return initial.bytes;
}

std::vector<std::uint8_t> SerializeStructural(MinimalApplication& model_root) {
  ByteSink structural;
  SerializeStructuralNodePublication(model_root, structural);
  return structural.bytes;
}

void PumpDeferred(std::deque<ModelObjectProxy::ModelWork>& deferred,
                  ae::Domain& model_domain) {
  while (!deferred.empty()) {
    auto work = std::move(deferred.front());
    deferred.pop_front();
    work(model_domain);
  }
}

void AssertMountSingleMainWindow() {
  CHECK(ApptraverseLifecycleCountDesktopWindows() == 1);
  CHECK(ApptraverseLifecycleCountMainButtons() == 1);
  CHECK(ApptraverseLifecycleCountAuxButtons() == 0);
}

void AssertMountDualWindows() {
  CHECK(ApptraverseLifecycleCountDesktopWindows() == 2);
  CHECK(ApptraverseLifecycleCountMainButtons() == 1);
  CHECK(ApptraverseLifecycleCountAuxButtons() == 1);
}

void AssertMountEmpty() {
  CHECK(ApptraverseLifecycleCountDesktopWindows() == 0);
  CHECK(ApptraverseLifecycleCountMainButtons() == 0);
  CHECK(ApptraverseLifecycleCountAuxButtons() == 0);
}

void RunSingleWindowSessionCycle(std::vector<std::uint8_t> const& initial_bytes,
                                 ui::web::WebPresentationHost& host,
                                 ModelObjectProxy& proxy, int baseline_emval) {
  {
    ApplicationUiSession session(initial_bytes, &host, proxy);
    AssertMountSingleMainWindow();
    ApptraverseLifecycleCaptureMainDomRefs();
  }
  AssertMountEmpty();
  ApptraverseLifecycleAssertDetachedClientClean();
  CHECK(ApptraverseCountEmvalHandles() == baseline_emval);
}

bool RunWebPresenterLifecycleTest() {
  EnsureObjectRegistration();
  EnsureMinimalApplicationRegistration();
  EnsureMinimalAppControlsRegistration();
  example::minimal::EnsureMinimalAppWindowEventsRegistration();
  ui::web::EnsureWebUiPresenterRegistration();

  std::vector<std::uint8_t> const initial_bytes = BuildInitialPublicationBytes();

  ui::web::WebPresentationHost host;
  host.mount = ApptraverseGetAppMountElement();

  std::atomic<std::size_t> proxy_work_count{0};
  std::deque<ModelObjectProxy::ModelWork> deferred;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    ++proxy_work_count;
    deferred.push_back(std::move(work));
  }};

  int const baseline_emval = ApptraverseCountEmvalHandles();

  RunSingleWindowSessionCycle(initial_bytes, host, proxy, baseline_emval);

  std::size_t const proxy_before_stale = proxy_work_count.load();
  ApptraverseLifecycleDispatchStaleMainControlEvents();
  CHECK(proxy_work_count.load() == proxy_before_stale);
  CHECK(deferred.empty());

  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto model_root = BuildWebMinimalApplicationGraph(model_domain);
  PrepareMinimalApplicationForRun(*model_root);

  std::vector<std::uint8_t> structural_open;
  {
    ApplicationUiSession session(initial_bytes, &host, proxy);
    AssertMountSingleMainWindow();
    auto open = MinimalOpenAuxiliaryWindowEvent::ptr::Create(
        ae::CreateWith{model_domain});
    model_root->Commit(std::move(open));
    PumpDeferred(deferred, model_domain);
    structural_open = SerializeStructural(*model_root);
    session.ApplyStructural(structural_open);
    AssertMountDualWindows();
    ApptraverseLifecycleCaptureMainDomRefs();
    ApptraverseLifecycleCaptureAuxDomRefs();
  }
  AssertMountEmpty();
  CHECK(ApptraverseCountEmvalHandles() == baseline_emval);

  std::size_t const proxy_before_aux_stale = proxy_work_count.load();
  ApptraverseLifecycleDispatchStaleAuxControlEvents();
  CHECK(proxy_work_count.load() == proxy_before_aux_stale);

  std::vector<std::uint8_t> structural_close;
  {
    ApplicationUiSession session(initial_bytes, &host, proxy);
    session.ApplyStructural(structural_open);
    AssertMountDualWindows();
    auto close = MinimalCloseAuxiliaryWindowEvent::ptr::Create(
        ae::CreateWith{model_domain});
    model_root->Commit(std::move(close));
    PumpDeferred(deferred, model_domain);
    structural_close = SerializeStructural(*model_root);
    session.ApplyStructural(structural_close);
    AssertMountSingleMainWindow();
    ApptraverseLifecycleClickMainApply();
    PumpDeferred(deferred, model_domain);
    session.ApplyStructural(SerializeStructural(*model_root));
  }
  AssertMountEmpty();
  CHECK(ApptraverseCountEmvalHandles() == baseline_emval);

  {
    ApplicationUiSession session(initial_bytes, &host, proxy);
    session.ApplyStructural(structural_close);
    AssertMountSingleMainWindow();
    session.ApplyStructural(structural_open);
    AssertMountDualWindows();
    session.ApplyStructural(structural_close);
    AssertMountSingleMainWindow();
    CHECK(ApptraverseLifecycleCountDesktopWindows() == 1);
    CHECK(ApptraverseLifecycleCountAuxButtons() == 0);
  }
  AssertMountEmpty();
  CHECK(ApptraverseCountEmvalHandles() == baseline_emval);

  RunSingleWindowSessionCycle(initial_bytes, host, proxy, baseline_emval);

  return true;
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  if (!apptraverse::test::RunWebPresenterLifecycleTest()) {
    return 1;
  }
  EM_ASM({
    globalThis.__webPresenterLifecycleDone = true;
    console.log('WEB_PRESENTER_LIFECYCLE_OK');
  });
  return 0;
}
