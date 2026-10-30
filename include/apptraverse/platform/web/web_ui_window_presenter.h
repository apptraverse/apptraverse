#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_

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

  ~WebDesktopWindowPresenter() override;

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

 private:
  void SyncGeometryFromMirror();

  // Main-thread DOM handles only; never created during model-graph construction.
  WebDesktopWindowDomRefs* dom_refs_{nullptr};
};

void EnsureWebUiPresenterRegistration();

}  // namespace apptraverse::ui::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_UI_WINDOW_PRESENTER_H_
