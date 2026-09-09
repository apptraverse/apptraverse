#include "dynamic_model.h"

#include <algorithm>
#include <cassert>

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Item);
APPTRAVERSE_REGISTER(ItemPresenter);
APPTRAVERSE_REGISTER(ItemList);
APPTRAVERSE_REGISTER(ItemListPresenter);
APPTRAVERSE_REGISTER(AddItemEvent);
APPTRAVERSE_REGISTER(RemoveItemEvent);
APPTRAVERSE_REGISTER(MainWindow);
APPTRAVERSE_REGISTER(MainWindowPresenter);
APPTRAVERSE_REGISTER(Application);

}  // namespace

void EnsureDynamicModelRegistration() {
  // Reference registrar storage so MSVC/lld keep this TU when linking the
  // static model library into tests/demos that do not pull lifecycle.cpp.
  (void)&g_apptraverse_registrar_Application;
}

void ItemList::Apply(AddItemEvent const& event) {
  assert(event.item.is_valid());
  assert(event.item.is_loaded());
  assert(event.item->list.is_valid());
  assert(&*event.item->list == this);
  items.push_back(event.item);
  NoteMaterializedChange();
}

void ItemList::Apply(RemoveItemEvent const& event) {
  assert(event.item.is_valid());
  assert(event.item.is_loaded());
  auto const it =
      std::find_if(items.begin(), items.end(), [&](Item::ptr const& item) {
        return item.is_valid() && &*item == &*event.item;
      });
  assert(it != items.end() && "RemoveItemEvent item must be live on Apply");
  items.erase(it);
  NoteMaterializedChange();
}

Item::ptr CommitAddItem(ItemList& list) {
  assert(list.domain != nullptr);
  auto item = Item::ptr::Create(ae::CreateWith{*list.domain});
  auto presenter = ItemPresenter::ptr::Create(ae::CreateWith{*list.domain});
  std::uint32_t next_number = 1;
  for (auto const& existing : list.items) {
    if (existing.is_valid() && existing.is_loaded() &&
        existing->number >= next_number) {
      next_number = existing->number + 1;
    }
  }
  item->number = next_number;
  item->list = ItemList::ptr::MakeFromThis(&list);
  item->presenter = presenter;
  presenter->item = item;

  auto event = AddItemEvent::ptr::Create(ae::CreateWith{*list.domain});
  event->item = item;
  list.Commit(event);
  return item;
}

bool CommitRemoveItem(ItemList& list, ae::ObjId item_id) {
  assert(list.domain != nullptr);
  auto const it =
      std::find_if(list.items.begin(), list.items.end(),
                   [&](Item::ptr const& item) {
                     return item.is_valid() && item->obj_id == item_id;
                   });
  if (it == list.items.end()) {
    // Stale / double remove: no-op. Normal GUI flow should not produce this
    // after the Item is already gone from live topology.
    return false;
  }
  auto event = RemoveItemEvent::ptr::Create(ae::CreateWith{*list.domain});
  event->item = *it;
  list.Commit(event);
  return true;
}

}  // namespace apptraverse
