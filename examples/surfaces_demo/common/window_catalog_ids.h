#ifndef APPTRAVERSE_WINDOW_CATALOG_IDS_H_
#define APPTRAVERSE_WINDOW_CATALOG_IDS_H_

#include "aether-objects/obj/obj_id.h"

namespace apptraverse::window_catalog_demo {

enum class ObjId : ae::ObjId::Type {
  Catalog = 1,
  MainWindow = 2,
  MainWindowPresenter = 3,
  CatalogList = 4,
  CatalogListPresenter = 5,
  AddButton = 6,
  AddButtonPresenter = 7,
  OpenButton = 8,
  OpenButtonPresenter = 9,
  DeleteButton = 10,
  DeleteButtonPresenter = 11,
  LanguageButton = 12,
  LanguageButtonPresenter = 13,
  Locale = 14,
};

inline constexpr ae::ObjId::Type ToObjId(ObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

}  // namespace apptraverse::window_catalog_demo

#endif  // APPTRAVERSE_WINDOW_CATALOG_IDS_H_
