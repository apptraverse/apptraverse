#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_

#include "minimal_app_controls.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::example::minimal {

struct MinimalMainPresenters {
  ui::UiPushButtonPresenter::ptr apply_button;
  ui::UiListContainerPresenter::ptr row_list;
  ui::UiPushButtonPresenter::ptr auxiliary_opener;

  AE_REFLECT_MEMBERS(apply_button, row_list, auxiliary_opener)
};

struct MinimalAuxiliaryPresenters {
  ui::UiPushButtonPresenter::ptr apply_button;
  ui::UiListContainerPresenter::ptr row_list;
  ui::UiPushButtonPresenter::ptr closer;

  AE_REFLECT_MEMBERS(apply_button, row_list, closer)
};

struct MinimalAuxiliaryWindowState {
  ui::UiDesktopWindow::ptr window;
  MinimalApplyButton::ptr apply_button;
  MinimalAppList::ptr row_list;
  MinimalAuxiliaryPresenters presenters;
  MinimalAuxiliaryCloserButton::ptr closer;

  AE_REFLECT_MEMBERS(window, apply_button, row_list, presenters, closer)
};

class MinimalOpenAuxiliaryWindowEvent;
class MinimalCloseAuxiliaryWindowEvent;

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
                    AE_MMBR(row_list), AE_MMBR(presenters),
                    AE_MMBR(auxiliary_opener), AE_MMBR(auxiliary),
                    AE_MMBR(auxiliary_presented_open))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, main_window, apply_button, row_list, presenters,
        auxiliary_opener, auxiliary, auxiliary_presented_open);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, main_window, apply_button, row_list, presenters,
        auxiliary_opener, auxiliary, auxiliary_presented_open);
  }

  ui::UiDesktopWindow::ptr main_window;
  MinimalApplyButton::ptr apply_button;
  MinimalAppList::ptr row_list;
  MinimalMainPresenters presenters;
  MinimalAuxiliaryOpenerButton::ptr auxiliary_opener;
  MinimalAuxiliaryWindowState auxiliary;
  bool auxiliary_presented_open;

  void Apply(MinimalOpenAuxiliaryWindowEvent const& event);
  void Apply(MinimalCloseAuxiliaryWindowEvent const& event);

  void LoadAuxiliaryPresentation();
  void OpenAuxiliaryWindowImpl();
  void CloseAuxiliaryWindowImpl();
};

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_
