#include "minimal_app_graph.h"

#include "minimal_app_graph_builder.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildMinimalApplicationGraph(ae::Domain& domain) {
  return BuildMinimalApplicationGraph(
      domain, [](ae::Domain& build_domain, ae::ObjId presenter_id) {
        return ui::windows::WinDesktopWindowPresenter::ptr::Create(
            ae::CreateWith{build_domain}.with_id(presenter_id));
      });
}

}  // namespace apptraverse::example::minimal
