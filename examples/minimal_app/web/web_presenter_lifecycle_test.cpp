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

EM_JS(int, ApptraverseCountEmvalHandles, (), {
  return Module.count_emval_handles();
});

EM_JS(emscripten::EM_VAL, ApptraverseGetAppMountElement, (), {
  return Emval.toHandle(document.getElementById('app-mount'));
});

EM_JS(void, ApptraverseLifecycleCaptureDomRefs, (), {
  var mount = document.getElementById('app-mount');
  var win = mount.querySelector('[data-apptraverse-desktop-window="1"]');
  if (!win) {
    throw new Error('lifecycle capture: missing desktop window');
  }
  globalThis.__apptraverseLifecycleCapture = {
    client: win.firstChild,
    button: mount.querySelector('[data-apptraverse-control-id="7101"]'),
    list: mount.querySelector('[data-apptraverse-control-id="7102"]')
  };
  if (!globalThis.__apptraverseLifecycleCapture.button ||
      !globalThis.__apptraverseLifecycleCapture.list) {
    throw new Error('lifecycle capture: missing button or list');
  }
});

EM_JS(int, ApptraverseLifecycleCountDesktopWindows, (), {
  return document.getElementById('app-mount')
      .querySelectorAll('[data-apptraverse-desktop-window="1"]').length;
});

EM_JS(int, ApptraverseLifecycleCountButtons, (), {
  return document.getElementById('app-mount')
      .querySelectorAll('[data-apptraverse-control-id="7101"]').length;
});

EM_JS(int, ApptraverseLifecycleCountLists, (), {
  return document.getElementById('app-mount')
      .querySelectorAll('[data-apptraverse-control-id="7102"]').length;
});

EM_JS(void, ApptraverseLifecycleAssertDetachedClientClean, (), {
  var cap = globalThis.__apptraverseLifecycleCapture;
  if (!cap || !cap.client) {
    throw new Error('lifecycle: missing captured client');
  }
  if (cap.client.querySelector('.apptraverse-push-button')) {
    throw new Error('lifecycle: client retained push-button node');
  }
  if (cap.client.querySelector('.apptraverse-list')) {
    throw new Error('lifecycle: client retained list node');
  }
});

EM_JS(void, ApptraverseLifecycleDispatchStaleControlEvents, (), {
  var cap = globalThis.__apptraverseLifecycleCapture;
  if (cap.button) {
    cap.button.dispatchEvent(new MouseEvent('click', { bubbles: true }));
  }
  if (cap.list) {
    cap.list.dispatchEvent(new MouseEvent('click', { bubbles: true }));
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

void AssertMountEmpty() {
  CHECK(ApptraverseLifecycleCountDesktopWindows() == 0);
  CHECK(ApptraverseLifecycleCountButtons() == 0);
  CHECK(ApptraverseLifecycleCountLists() == 0);
}

void AssertMountPopulated() {
  CHECK(ApptraverseLifecycleCountDesktopWindows() == 1);
  CHECK(ApptraverseLifecycleCountButtons() == 1);
  CHECK(ApptraverseLifecycleCountLists() == 1);
}

void RunSessionCycle(std::vector<std::uint8_t> const& initial_bytes,
                     ui::web::WebPresentationHost& host,
                     ModelObjectProxy& proxy, int baseline_emval) {
  {
    ApplicationUiSession session(initial_bytes, &host, proxy);
    AssertMountPopulated();
    ApptraverseLifecycleCaptureDomRefs();
  }
  AssertMountEmpty();
  ApptraverseLifecycleAssertDetachedClientClean();
  CHECK(ApptraverseCountEmvalHandles() == baseline_emval);
}

bool RunWebPresenterLifecycleTest() {
  EnsureObjectRegistration();
  EnsureMinimalApplicationRegistration();
  EnsureMinimalAppControlsRegistration();
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

  RunSessionCycle(initial_bytes, host, proxy, baseline_emval);

  std::size_t const proxy_before_stale = proxy_work_count.load();
  ApptraverseLifecycleDispatchStaleControlEvents();
  CHECK(proxy_work_count.load() == proxy_before_stale);
  CHECK(deferred.empty());

  RunSessionCycle(initial_bytes, host, proxy, baseline_emval);

  {
    ApplicationUiSession session(initial_bytes, &host, proxy);
    AssertMountPopulated();
  }
  AssertMountEmpty();
  CHECK(ApptraverseCountEmvalHandles() == baseline_emval);

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
