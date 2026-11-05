#include "minimal_app_graph_macos.h"

#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "minimal_app_graph_builder.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildMacMinimalApplicationGraph(ae::Domain& domain) {
  ui::macos::EnsureMacUiPresenterRegistration();
  MinimalPresenterFactories factories;
  factories.create_window = [](ae::Domain& build_domain,
                               ae::ObjId presenter_id) {
    return ui::macos::MacDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_button = [](ae::Domain& build_domain,
                               ae::ObjId presenter_id) {
    return ui::macos::MacPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_list = [](ae::Domain& build_domain,
                             ae::ObjId presenter_id) {
    return ui::macos::MacListContainerPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  return BuildMinimalApplicationGraph(domain, factories);
}

}  // namespace apptraverse::example::minimal
