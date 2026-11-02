#include "minimal_app_graph_linux.h"

#include "apptraverse/platform/linux/linux_ui_list_presenter.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"
#include "apptraverse/platform/linux/linux_ui_registration.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"

#include "minimal_app_graph_builder.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildLinuxMinimalApplicationGraph(ae::Domain& domain) {
  ui::linux::EnsureLinuxUiPresenterRegistration();
  MinimalPresenterFactories factories;
  factories.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::linux::LinuxDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::linux::LinuxPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_list = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::linux::LinuxListContainerPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  return BuildMinimalApplicationGraph(domain, factories);
}

}  // namespace apptraverse::example::minimal
