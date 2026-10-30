#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_UI_PUSH_BUTTON_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_UI_PUSH_BUTTON_PRESENTER_H_

#include <emscripten/val.h>

#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::ui::web {

struct WebPushButtonDomRefs;

class WebPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::web::WebPushButtonPresenter",
                           WebPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  WebPushButtonPresenter() = default;

 public:
  explicit WebPushButtonPresenter(ae::ObjProp prop)
      : UiPushButtonPresenter{prop} {}

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

  bool ReadyForPresentation() const override;

 private:
  void SyncFromMirror();

  WebPushButtonDomRefs* dom_refs_{nullptr};
};

}  // namespace apptraverse::ui::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_UI_PUSH_BUTTON_PRESENTER_H_
