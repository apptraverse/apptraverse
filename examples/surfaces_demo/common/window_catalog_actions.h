#ifndef APPTRAVERSE_WINDOW_CATALOG_ACTIONS_H_
#define APPTRAVERSE_WINDOW_CATALOG_ACTIONS_H_

#include "window_catalog_model.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::example::surfaces {

class CatalogAddButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogAddButton",
                           CatalogAddButton, ui::UiPushButton, 0)

 protected:
  CatalogAddButton() = default;

 public:
  explicit CatalogAddButton(ae::ObjProp prop) : UiPushButton{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  WindowCatalog::ptr catalog;

  void HandlePushButtonClicked() override;
};

class CatalogOpenButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogOpenButton",
                           CatalogOpenButton, ui::UiPushButton, 0)

 protected:
  CatalogOpenButton() = default;

 public:
  explicit CatalogOpenButton(ae::ObjProp prop) : UiPushButton{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  WindowCatalog::ptr catalog;

  void HandlePushButtonClicked() override;
};

class CatalogDeleteButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDeleteButton",
      CatalogDeleteButton, ui::UiPushButton, 0)

 protected:
  CatalogDeleteButton() = default;

 public:
  explicit CatalogDeleteButton(ae::ObjProp prop) : UiPushButton{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  WindowCatalog::ptr catalog;

  void HandlePushButtonClicked() override;
};

class CatalogLanguageButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogLanguageButton",
      CatalogLanguageButton, ui::UiPushButton, 0)

 protected:
  CatalogLanguageButton() = default;

 public:
  explicit CatalogLanguageButton(ae::ObjProp prop) : UiPushButton{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  WindowCatalog::ptr catalog;

  void HandlePushButtonClicked() override;
};

void EnsureCatalogActionRegistration();

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_WINDOW_CATALOG_ACTIONS_H_
