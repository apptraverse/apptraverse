#ifndef APPTRAVERSE_DYNAMIC_DISTILL_H_
#define APPTRAVERSE_DYNAMIC_DISTILL_H_

#include "aether-objects/obj/domain.h"

#include "dynamic_model.h"

namespace apptraverse {

// Dev-only initial graph construction. Link only into distillation builds and
// tests that need a fresh fixture. Load-only executables must not link this.
struct DynamicPresenterFactories {
  std::function<MainWindowPresenter::ptr(ae::Domain&, ae::ObjId)> window;
  std::function<AddItemPresenter::ptr(ae::Domain&, ae::ObjId)> add;
  std::function<ItemListPresenter::ptr(ae::Domain&, ae::ObjId)> list;
  std::function<ItemPresenter::ptr(ae::Domain&, ae::ObjId)> item;
};
Application::ptr BuildDynamicObjectsGraph(ae::Domain& domain, DynamicPresenterFactories factories = {});

}  // namespace apptraverse

#endif  // APPTRAVERSE_DYNAMIC_DISTILL_H_
