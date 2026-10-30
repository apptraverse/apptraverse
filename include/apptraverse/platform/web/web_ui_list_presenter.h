#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_UI_LIST_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_UI_LIST_PRESENTER_H_

#include <emscripten/val.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::web {

struct WebListContainerDomRefs;

class WebListContainerPresenter : public UiListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::web::WebListContainerPresenter",
                           WebListContainerPresenter, UiListContainerPresenter,
                           0)

 protected:
  WebListContainerPresenter() = default;

 public:
  explicit WebListContainerPresenter(ae::ObjProp prop)
      : UiListContainerPresenter{prop} {}

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

  void HandleDomSelection(int row_index);
  void HandleDomActivation(int row_index);

 private:
  void SyncFromMirror();

  WebListContainerDomRefs* dom_refs_{nullptr};
  bool applying_mirror_sync_{false};
};

}  // namespace apptraverse::ui::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_UI_LIST_PRESENTER_H_
