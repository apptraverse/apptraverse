#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_

#include <emscripten/val.h>
#include <vector>
#include "apptraverse/ui/ui_device_state.h"
#include "apptraverse/ui/ui_constraint_layout.h"

#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::web {

struct WebDesktopWindowDomRefs;
UiDeviceEnvironment ReadWebDeviceEnvironment();

class WebDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(::apptraverse::ui::native_schema::kWindow,
                           WebDesktopWindowPresenter,
                           UiDesktopWindowPresenter, 0)

 protected:
  WebDesktopWindowPresenter() = default;

 public:
  explicit WebDesktopWindowPresenter(ae::ObjProp prop)
      : UiDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  void* NativeChildParentHandle() const override;

  emscripten::EM_VAL ClientMountHandle() const;
  UiControlLayout ResolveChildLayout(UiControlLayout requested) const override;
  void RegisterChildControl(emscripten::EM_VAL child, std::uint32_t control_id);
  void UnregisterChildControl(emscripten::EM_VAL child);
  void RefreshLocalPresentation(bool environment_changed = false);
  void RefreshLayout(bool require_complete = false);


 private:
  void SyncGeometryFromMirror();
  void SyncTitleFromMirror();

  bool NativeMeasurementsReady() const;
  UiConstraintLayoutResult ComputeLocalLayout() const;
  struct NativeChild { emscripten::EM_VAL element; std::uint32_t control_id; };
  std::vector<NativeChild> native_children_;
  bool applying_layout_{false};
  WebDesktopWindowDomRefs* dom_refs_{nullptr};
};

void EnsureWebUiPresenterRegistration();

}  // namespace apptraverse::ui::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_
