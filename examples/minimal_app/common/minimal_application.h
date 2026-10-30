#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_

#include "minimal_app_controls.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::example::minimal {

struct MinimalApplicationPresenters {
  ui::UiPushButtonPresenter::ptr apply_button;
  ui::UiListContainerPresenter::ptr row_list;

  AE_REFLECT_MEMBERS(apply_button, row_list)
};

ae::ObjId MinimalApplicationRootId();
ae::ObjId MinimalMainWindowId();
ae::ObjId MinimalPresenterId();

void EnsureMinimalApplicationRegistration();

class MinimalApplication : public NodeFor<MinimalApplication> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::minimal::MinimalApplication",
                           MinimalApplication, Node, 0)

 protected:
  MinimalApplication() = default;

 public:
  explicit MinimalApplication(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(main_window), AE_MMBR(apply_button),
                    AE_MMBR(row_list), AE_MMBR(presenters))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, main_window, apply_button, row_list, presenters);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, main_window, apply_button, row_list, presenters);
  }

  ui::UiDesktopWindow::ptr main_window;
  MinimalApplyButton::ptr apply_button;
  MinimalAppList::ptr row_list;
  MinimalApplicationPresenters presenters;
};

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_
