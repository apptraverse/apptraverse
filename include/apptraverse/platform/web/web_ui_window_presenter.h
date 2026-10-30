#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_

#include <emscripten/val.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::web {

struct WebDesktopWindowDomRefs;

class WebDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::web::WebDesktopWindowPresenter",
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

  void NotifyUserCloseFromUi();

 private:
  void SyncGeometryFromMirror();
  void SyncTitleFromMirror();

  WebDesktopWindowDomRefs* dom_refs_{nullptr};
};

void EnsureWebUiPresenterRegistration();

}  // namespace apptraverse::ui::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_
