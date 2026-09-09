#include "dynamic_model.h"

#include <algorithm>
#include <cassert>

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Item);
APPTRAVERSE_REGISTER(ItemPresenter);
APPTRAVERSE_REGISTER(ItemList);
APPTRAVERSE_REGISTER(ItemListPresenter);
APPTRAVERSE_REGISTER(AddItem);
APPTRAVERSE_REGISTER(AddItemPresenter);
APPTRAVERSE_REGISTER(AddItemEvent);
APPTRAVERSE_REGISTER(RemoveItemEvent);
APPTRAVERSE_REGISTER(MainWindow);
APPTRAVERSE_REGISTER(MainWindowPresenter);
APPTRAVERSE_REGISTER(Application);

}  // namespace

void EnsureDynamicModelRegistration() {
  (void)&g_apptraverse_registrar_Application;
  (void)&g_apptraverse_registrar_AddItem;
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

void AddItem::Click() {
  assert(window.is_valid() && window.is_loaded());
  assert(window->item_list.is_valid() && window->item_list.is_loaded());
  ItemList& list = *window->item_list;
  assert(list.domain != nullptr);

  auto item = Item::ptr::Create(ae::CreateWith{*list.domain});
  auto item_presenter =
      ItemPresenter::ptr::Create(ae::CreateWith{*list.domain});
  std::uint32_t next_number = 1;
  for (auto const& existing : list.items) {
    if (existing.is_valid() && existing.is_loaded() &&
        existing->number >= next_number) {
      next_number = existing->number + 1;
    }
  }
  item->number = next_number;
  item->list = window->item_list;
  item->presenter = item_presenter;
  item_presenter->item = item;

  auto event = AddItemEvent::ptr::Create(ae::CreateWith{*list.domain});
  event->item = item;
  list.Commit(event);
}

void Item::Remove() {
  if (!list.is_valid() || !list.is_loaded()) {
    return;
  }
  ItemList& item_list = *list;
  assert(item_list.domain != nullptr);
  auto const it =
      std::find_if(item_list.items.begin(), item_list.items.end(),
                   [&](Item::ptr const& entry) {
                     return entry.is_valid() && &*entry == this;
                   });
  if (it == item_list.items.end()) {
    return;
  }
  auto event = RemoveItemEvent::ptr::Create(ae::CreateWith{*item_list.domain});
  event->item = Item::ptr::MakeFromThis(this);
  item_list.Commit(event);
}

void AddItemPresenter::Click() {
  assert(model_proxy != nullptr);
  assert(add_item.is_valid());
  model_proxy->Invoke<AddItem>(add_item->obj_id, &AddItem::Click);
}

void ItemPresenter::RemoveClick() {
  assert(model_proxy != nullptr);
  assert(item.is_valid());
  model_proxy->Invoke<Item>(item->obj_id, &Item::Remove);
}

}  // namespace apptraverse
