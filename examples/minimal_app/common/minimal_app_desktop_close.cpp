#include "minimal_app_desktop_close.h"

#include "minimal_application.h"
#include "minimal_app_window_events.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse::example::minimal {

void HandleMinimalAppDesktopWindowClose(
    ae::Obj& application_root, ae::Obj& closed_desktop_window,
    ModelObjectProxy& proxy, std::function<void()> request_application_stop) {
  auto& application = static_cast<MinimalApplication&>(application_root);
  if (closed_desktop_window.obj_id == MinimalMainWindowId()) {
    request_application_stop();
    return;
  }

  application.auxiliary.window.Load();
  if (application.auxiliary.window.is_valid() &&
      application.auxiliary.window.id() == closed_desktop_window.obj_id) {
    proxy.EnqueueModelWork([](ae::Domain& domain) {
      ae::Ptr<ae::Obj> const root = domain.Find(MinimalApplicationRootId());
      if (!root) {
        return;
      }
      auto& model_app = static_cast<MinimalApplication&>(*root.operator->());
      auto event = MinimalCloseAuxiliaryWindowEvent::ptr::Create(
          ae::CreateWith{domain});
      model_app.Commit(std::move(event));
    });
  }
}

}  // namespace apptraverse::example::minimal
