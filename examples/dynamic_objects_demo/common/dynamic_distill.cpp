#include "dynamic_distill.h"

#include "dynamic_ids.h"

namespace apptraverse {

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

  list->window = window;
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

}  // namespace apptraverse
