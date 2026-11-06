#ifndef APPTRAVERSE_WINDOW_CATALOG_CLOSE_BUTTON_H_
#define APPTRAVERSE_WINDOW_CATALOG_CLOSE_BUTTON_H_

#include "window_catalog_model.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::example::surfaces {

class CatalogCloseButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogCloseButton",
                           CatalogCloseButton, ui::UiPushButton, 0)

 protected:
  CatalogCloseButton() = default;

 public:
  explicit CatalogCloseButton(ae::ObjProp prop) : UiPushButton{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog), AE_MMBR(window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog, window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog, window);
  }

  WindowCatalog::ptr catalog;
  CatalogWindow::ptr window;

  void HandlePushButtonClicked() override;
};

void EnsureCatalogCloseButtonRegistration();

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_WINDOW_CATALOG_CLOSE_BUTTON_H_
