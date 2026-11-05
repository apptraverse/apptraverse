#include "minimal_app_graph_ios.h"

#include "apptraverse/platform/ios/ios_ui_presenters.h"
#include "minimal_app_graph_builder.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildIosMinimalApplicationGraph(ae::Domain& domain) {
  ui::ios::EnsureIosUiPresenterRegistration();
  MinimalPresenterFactories factories;
  factories.create_window = [](ae::Domain& build_domain,
                               ae::ObjId presenter_id) {
    return ui::ios::IosDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_button = [](ae::Domain& build_domain,
                               ae::ObjId presenter_id) {
    return ui::ios::IosPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_list = [](ae::Domain& build_domain,
                             ae::ObjId presenter_id) {
    return ui::ios::IosListContainerPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  return BuildMinimalApplicationGraph(domain, factories);
}

}  // namespace apptraverse::example::minimal
