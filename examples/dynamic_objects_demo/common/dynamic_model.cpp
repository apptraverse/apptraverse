#include "dynamic_model.h"

#include <cassert>

#include "aether-objects/obj/domain.h"

#include "apptraverse/runtime_node.h"

namespace apptraverse {

void ItemList::Apply(AddItemEvent const& event) {
  assert(event.item.is_valid());
  assert(event.item.is_loaded());
  assert(event.item->list.is_valid());
  assert(&*event.item->list == this);
  items.push_back(event.item);
  NoteMaterializedChange();
}

Application::ptr BuildDynamicObjectsGraph(ae::Domain& domain) {
  using dynamic_objects::ObjId;
  using dynamic_objects::ToObjId;

  auto application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Application)));
  auto window = MainWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindow)));
  auto window_presenter = MainWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindowPresenter)));
  auto list = ItemList::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::ItemList)));
  auto list_presenter = ItemListPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::ItemListPresenter)));
  auto item1 = Item::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Item1)));
  auto item1_presenter = ItemPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Item1Presenter)));

  window->x = dynamic_objects::kDefaultX;
  window->y = dynamic_objects::kDefaultY;
  window->width = dynamic_objects::kDefaultWidth;
  window->height = dynamic_objects::kDefaultHeight;
  window->presenter = window_presenter;
  window->item_list = list;
  window_presenter->window = window;

  list->presenter = list_presenter;
  list_presenter->list = list;

  item1->number = 1;
  item1->list = list;
  item1->presenter = item1_presenter;
  item1_presenter->item = item1;
  list->items.push_back(item1);

  application->main_window = window;
  return application;
}

Item::ptr CommitAddItem(ItemList& list) {
  assert(list.domain != nullptr);
  auto item = Item::ptr::Create(ae::CreateWith{*list.domain});
  auto presenter = ItemPresenter::ptr::Create(ae::CreateWith{*list.domain});
  item->number = static_cast<std::uint32_t>(list.items.size() + 1);
  item->list = ItemList::ptr::MakeFromThis(&list);
  item->presenter = presenter;
  presenter->item = item;

  auto event = AddItemEvent::ptr::Create(ae::CreateWith{*list.domain});
  event->item = item;
  list.Commit(event);
  return item;
}

}  // namespace apptraverse
