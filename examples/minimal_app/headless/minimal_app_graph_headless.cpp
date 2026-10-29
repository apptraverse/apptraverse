#include "minimal_app_graph_headless.h"

#include "minimal_app_graph_builder.h"

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildHeadlessMinimalApplicationGraph(
    ae::Domain& domain) {
  return BuildMinimalApplicationGraph(
      domain, [](ae::Domain& build_domain, ae::ObjId presenter_id) {
        return ui::UiDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{build_domain}.with_id(presenter_id));
      });
}

void EnsureHeadlessMinimalApplicationGraphRegistration() {
  EnsureMinimalApplicationRegistration();
  ui::EnsureUiPresenterRegistration();
}

}  // namespace apptraverse::example::minimal
