#ifndef APPTRAVERSE_PRESENTER_H_
#define APPTRAVERSE_PRESENTER_H_

#include <cstdint>

#include "aether-objects/obj/obj.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

// Platform-neutral presenter base. Not a Node: no journal. Local presenter
// state is not model-journaled; GUI may mutate it directly later.
//
// OnLoad / OnUnload are GUI presentation hooks, not object-system
// deserialization callbacks. Ordinary Load/Create/Save must not create or
// destroy native resources. Call OnLoad only from InitializePresenters after
// the GUI mirror graph is fully loaded and references are resolved. Call
// OnUnload only from UnloadPresenters for a graph that completed that pass.
// The object destructor does not tear down native presentation.
class Presenter : public ae::Obj {
  APPTRAVERSE_OBJECT(Presenter, ae::Obj, 0)

 protected:
  Presenter() = default;

 public:
  explicit Presenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  // Native presentation initialization. Called from InitializePresenters /
  // InitializeNewPresenters after the GUI graph is complete. Not object Load.
  virtual void OnLoad() {}
  // Existing GUI mirror was updated by a model publication. Sync native
  // presentation with the mirror. Not a second OnLoad.
  virtual void OnModelChanged() {}
  // Native teardown. Called once from UnloadPresenters.
  virtual void OnUnload() {}

  // True when this presenter may run OnLoad (parents already presented).
  // Default: always ready. Child presenters override when they need a parent
  // HWND created by another presenter's OnLoad.
  virtual bool ReadyForPresentation() const { return true; }

  // Platform input dispatch (Win32 WM_COMMAND control id + notification).
  // Default: unhandled. Runtime-only; not reflected. Does not take HWND.
  virtual bool OnCommand(std::uint32_t command_id,
                         std::uint16_t notification_code) {
    (void)command_id;
    (void)notification_code;
    return false;
  }

  // Runtime-only. Not serialized. Win32 Main uses this as the notify HWND
  // for shutdown, not as CreateWindow lpParam.
  void* presentation_host{nullptr};

  // Runtime-only. Not serialized. Queues ObjId-targeted method invokes onto
  // the model Domain. Distinct from presentation_host (native HWND/host).
  class ModelObjectProxy* model_proxy{nullptr};

  // Runtime-only. Set by InitializePresenters / InitializeNewPresenters after
  // OnLoad. Used so incremental graph growth activates only new presenters.
  bool presentation_loaded{false};

  // Runtime-only. Monotonic OnLoad sequence for this process. Unload uses
  // descending order so children tear down before parents, independent of
  // ObjId / Save collect order. Not serialized.
  std::uint64_t presentation_load_order{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_PRESENTER_H_
