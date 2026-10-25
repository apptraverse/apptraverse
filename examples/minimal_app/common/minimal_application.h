#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse::example::minimal {

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

  AE_OBJECT_REFLECT(AE_MMBR(main_window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, main_window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, main_window);
  }

  ui::UiDesktopWindow::ptr main_window;
};

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_
