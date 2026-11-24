#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include "apptraverse/platform/web/web_idbfs.h"
#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/platform/web/web_ui_list_presenter.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"
#include "apptraverse/platform/web/web_ui_text_control_presenter.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "chat_ui_session.h"
namespace gui = apptraverse::chat::gui;
namespace {
std::unique_ptr<gui::Session> session;
std::string profile;
auto* queue = em_proxying_queue_create();
struct Barrier {
  std::mutex mutex;
  std::condition_variable ready;
  bool done{}, ok{};
};
// clang-format off
EM_JS(void, BeginTransportStore, (Barrier * barrier), {
  var mount =
      FS.lookupPath('/shared-network', {follow_mount : true}).node.mount;
  IDBFS.syncfs(
      mount, false,
      function(error) { Module._SharedStored(barrier, error ? 0 : 1); });
});
bool FlushTransport() {
  Barrier barrier;
  if (!emscripten_proxy_async(
          queue, emscripten_main_runtime_thread_id(),
          [](void* value) {
            BeginTransportStore(static_cast<Barrier*>(value));
          },
          &barrier))
    return false;
  std::unique_lock lock(barrier.mutex);
  barrier.ready.wait(lock, [&] { return barrier.done; });
  return barrier.ok;
}
apptraverse::ui::web::WebPresentationHost host;
emscripten::val mount = emscripten::val::undefined();
std::uint64_t copied{};
EM_JS(void, CopyUid, (char const* uid), {
  navigator.clipboard.writeText(UTF8ToString(Number(uid)))
      .then(() => Module._SharedCopyResult(1),
            () => Module._SharedCopyResult(0));
});
void Render() {
  if (session) {
    session->ApplyPublication(&host);
    if (auto* model = session->Mirror()) {
      if (model->copy_request != copied) {
        copied = model->copy_request;
        CopyUid(model->connection.uid.c_str());
      }
    }
  }
}
void Changed() {
  if (!emscripten_proxy_async(
          queue, emscripten_main_runtime_thread_id(), [](void*) { Render(); },
          nullptr))
    std::abort();
}
EM_JS(void, ShowChatLoadError, (), {
  document.getElementById('loading').textContent = 'Could not load saved state';
});
EM_JS(void, InstallLifecycle, (), {
  addEventListener('pagehide', () => Module._SharedSave());
  document.addEventListener(
      'visibilitychange', () => {
        if (document.hidden) Module._SharedSave();
      });
});
EM_JS(void, EnableWebPush, (), {
  if (window.setupWebPush) window.setupWebPush();
});
}  // namespace
extern "C" {
EMSCRIPTEN_KEEPALIVE void SharedStored(Barrier* barrier, int ok) {
  std::lock_guard lock(barrier->mutex);
  barrier->ok = ok;
  barrier->done = true;
  barrier->ready.notify_one();
}
EMSCRIPTEN_KEEPALIVE void SharedCopyResult(int ok) {
  if (session)
    session->Proxy().Invoke(gui::RootId(), &gui::ChatUi::ClipboardResult,
                            ok != 0);
}
EMSCRIPTEN_KEEPALIVE void SharedRegisterWebPush(const char* endpoint) {
  if (session) session->RegisterWebPush(endpoint);
}
EMSCRIPTEN_KEEPALIVE int SharedSave() {
  if (!session || !session->Mirror()) return 1;
  std::vector<std::uint8_t> bytes;
  std::filesystem::path file;
  return session->CaptureLocal(bytes, &file) &&
         apptraverse::web::CommitIdbfsFileForPageLifecycle(
             "/shared-ui", file.generic_string(), bytes);
}
EMSCRIPTEN_KEEPALIVE void SharedStart(const char* who, const char* invited,
                                      const char* profile_name, int is_host,
                                      int load_only, const char* push_service) {
  auto name = std::string{who}, invite = std::string{invited},
       key = std::string{profile_name}, push = std::string{push_service};
  profile = "/shared-ui/" + key;
  apptraverse::web::MountIdbfsAndSyncLoad(
      "/shared-ui",
      [name, invite, key, is_host, load_only, push] {
        apptraverse::web::MountIdbfsAndSyncLoad(
            "/shared-network",
            [name, invite, key, is_host, load_only, push] {
              using namespace apptraverse::ui::web;
              EnsureWebUiPresenterRegistration();
              gui::EnsureUiRegistration();
              auto environment = ReadWebDeviceEnvironment();
              gui::ConfigureUiFactories(
                  {[](ae::Domain& d) {
                     return WebDesktopWindowPresenter::ptr::Create(
                         ae::CreateWith{d});
                   },
                   [](ae::Domain& d) {
                     return WebPushButtonPresenter::ptr::Create(
                         ae::CreateWith{d});
                   },
                   [](ae::Domain& d) {
                     return WebTextControlPresenter::ptr::Create(
                         ae::CreateWith{d});
                   },
                   [](ae::Domain& d) {
                     return WebListContainerPresenter::ptr::Create(
                         ae::CreateWith{d});
                   },
                   [environment](apptraverse::ui::UiDeviceState& state) {
                     state.ObserveEnvironment(
                         environment,
                         apptraverse::ui::UiEnvironmentReason::kStartup);
                   }});
              mount = emscripten::val::global("document")
                          .call<emscripten::val>("getElementById",
                                                 std::string{"chat"});
              host.mount = mount.as_handle();
              session = std::make_unique<gui::Session>(
                  profile, name, is_host, invite, Changed, load_only,
                  FlushTransport, "/shared-network/" + key);
              if (!push.empty()) session->ConfigurePush(push);
              session->Start();
              InstallLifecycle();
              EnableWebPush();
              emscripten_run_script(
                  "document.getElementById('loading').style.display='none'");
            },
            ShowChatLoadError);
      },
      ShowChatLoadError);
}
EM_JS(void, BeginQuery, (), {
  var query = new URLSearchParams(location.search),
      profile = query.get('profile') || 'default';
  if (!/^[A-Za-z0-9_-]+$/.test(profile))
    throw new Error('Invalid profile');
  var args =
      [
        query.get('name') || 'Web', query.get('invite') ||'', profile,
        query.get('pushService') ||''
      ]
          .map(value => stringToNewUTF8(value));
  Module._SharedStart(args[0], args[1], args[2],
                      query.get('role') === 'client' ? 0 : 1,
                      query.get('loadOnly') === '1' ? 1 : 0, args[3]);
  args.forEach(ptr => _free(ptr));
});
EMSCRIPTEN_KEEPALIVE void SharedBoot() { BeginQuery(); }
}
int main() { return 0; }

// clang-format on
