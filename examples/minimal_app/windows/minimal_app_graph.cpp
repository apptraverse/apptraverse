#include "minimal_app_graph.h"

#include "minimal_app_graph_builder.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildMinimalApplicationGraph(ae::Domain& domain) {
  MinimalPresenterFactories factories;
  factories.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::windows::WinDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::windows::WinPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_list = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::windows::WinListContainerPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  return BuildMinimalApplicationGraph(domain, factories);
}

}  // namespace apptraverse::example::minimal
