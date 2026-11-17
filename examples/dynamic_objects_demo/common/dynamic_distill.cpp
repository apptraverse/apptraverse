#include "dynamic_distill.h"

#include "dynamic_ids.h"

namespace apptraverse {

Application::ptr BuildDynamicObjectsGraph(ae::Domain& domain, DynamicPresenterFactories factories) {
  using dynamic_objects::ObjId;
  using dynamic_objects::ToObjId;

  auto application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Application)));
  auto window = MainWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindow)));
  ae::ObjId window_presenter_id{ToObjId(ObjId::MainWindowPresenter)};
  auto window_presenter = factories.window ? factories.window(domain, window_presenter_id) : MainWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(window_presenter_id));
  auto add_item = AddItem::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::AddItem)));
  ae::ObjId add_item_presenter_id{ToObjId(ObjId::AddItemPresenter)};
  auto add_item_presenter = factories.add ? factories.add(domain, add_item_presenter_id) : AddItemPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(add_item_presenter_id));
  auto list = ItemList::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::ItemList)));
  ae::ObjId list_presenter_id{ToObjId(ObjId::ItemListPresenter)};
  auto list_presenter = factories.list ? factories.list(domain, list_presenter_id) : ItemListPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(list_presenter_id));
  auto item1 = Item::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Item1)));
  ae::ObjId item1_presenter_id{ToObjId(ObjId::Item1Presenter)};
  auto item1_presenter = factories.item ? factories.item(domain, item1_presenter_id) : ItemPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(item1_presenter_id));

  window->x = dynamic_objects::kDefaultX;
  window->y = dynamic_objects::kDefaultY;
  window->width = dynamic_objects::kDefaultWidth;
  window->height = dynamic_objects::kDefaultHeight;
  window->presenter = window_presenter;
  window_presenter->window = window;

  window->add_item = add_item;
  add_item->window = window;
  add_item->presenter = add_item_presenter;
  add_item_presenter->add_item = add_item;

  window->item_list = list;
  list->window = window;
  list->presenter = list_presenter;
  list_presenter->list = list;

  item1->number = 1;
  item1->list = list;
  item1->presenter = item1_presenter;
  item1_presenter->item = item1;
  list->items.push_back(item1);
  if (factories.item) list->create_item_presenter = [factory = factories.item](ae::Domain& d) { return factory(d, ae::ObjId{}); };

  application->main_window = window;
  return application;
}

}  // namespace apptraverse
