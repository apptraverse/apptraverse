#include "minimal_app_graph_headless.h"

#include "minimal_app_graph_builder.h"

#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildHeadlessMinimalApplicationGraph(
    ae::Domain& domain) {
  MinimalPresenterFactories factories;
  factories.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::UiDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::UiPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factories.create_list = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::UiListContainerPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  return BuildMinimalApplicationGraph(domain, factories);
}

void EnsureHeadlessMinimalApplicationGraphRegistration() {
  EnsureMinimalApplicationRegistration();
  ui::EnsureUiPresenterRegistration();
}

}  // namespace apptraverse::example::minimal
