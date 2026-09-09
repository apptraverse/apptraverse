#ifndef APPTRAVERSE_DYNAMIC_IDS_H_
#define APPTRAVERSE_DYNAMIC_IDS_H_

#include <cstdint>

#include "aether-objects/obj/obj_id.h"

namespace apptraverse::dynamic_objects {

enum class ObjId : ae::ObjId::Type {
  Application = 1,
  MainWindow = 2,
  MainWindowPresenter = 3,
  ItemList = 4,
  ItemListPresenter = 5,
  Item1 = 6,
  Item1Presenter = 7,
  AddItem = 8,
  AddItemPresenter = 9,
};

inline constexpr ae::ObjId::Type ToObjId(ObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

inline constexpr std::int32_t kDefaultX = 100;
inline constexpr std::int32_t kDefaultY = 100;
inline constexpr std::int32_t kDefaultWidth = 420;
inline constexpr std::int32_t kDefaultHeight = 360;

}  // namespace apptraverse::dynamic_objects

#endif  // APPTRAVERSE_DYNAMIC_IDS_H_
