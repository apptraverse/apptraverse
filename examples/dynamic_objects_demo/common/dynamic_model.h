#ifndef APPTRAVERSE_DYNAMIC_MODEL_H_
#define APPTRAVERSE_DYNAMIC_MODEL_H_

#include <cstdint>
#include <vector>

#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "dynamic_ids.h"

namespace apptraverse {

class Item;
class ItemPresenter;
class ItemList;
class ItemListPresenter;
class MainWindow;
class MainWindowPresenter;
class AddItemEvent;

// Dynamic child. Not a Node: topology mutations are journaled on ItemList.
class Item : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::Item", Item, ae::Obj,
                           0)

 protected:
  Item() = default;

 public:
  explicit Item(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(number), AE_MMBR(list), AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, number, list, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, number, list, presenter);
  }

  std::uint32_t number{0};
  ae::ObjPtr<ItemList> list;
  ae::ObjPtr<ItemPresenter> presenter;
};

class ItemPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::ItemPresenter",
                           ItemPresenter, Presenter, 0)

 protected:
  ItemPresenter() = default;

 public:
  explicit ItemPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(item))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, item);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, item);
  }

  Item::ptr item;
};

class ItemList : public NodeFor<ItemList> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::ItemList", ItemList,
                           Node, 0)

 protected:
  ItemList() = default;

 public:
  explicit ItemList(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(items), AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(items, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(items, presenter);
  }

  std::vector<Item::ptr> items;
  ae::ObjPtr<ItemListPresenter> presenter;

  void Apply(AddItemEvent const& event);
};

class ItemListPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::ItemListPresenter",
                           ItemListPresenter, Presenter, 0)

 protected:
  ItemListPresenter() = default;

 public:
  explicit ItemListPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(list))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, list);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, list);
  }

  ItemList::ptr list;
};

// Event carries the Item object. Replay pushes the same ObjId; Apply must
// not Create a new Item.
class AddItemEvent : public EventFor<ItemList, AddItemEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::AddItemEvent",
                           AddItemEvent, Event, 0)

 protected:
  AddItemEvent() = default;

 public:
  explicit AddItemEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(item))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, item);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, item);
  }

  Item::ptr item;
};

class MainWindow : public NodeFor<MainWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::MainWindow",
                           MainWindow, Node, 0)

 protected:
  MainWindow() = default;

 public:
  explicit MainWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(item_list), AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(x, y, width, height, item_list, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(x, y, width, height, item_list, presenter);
  }

  std::int32_t x{dynamic_objects::kDefaultX};
  std::int32_t y{dynamic_objects::kDefaultY};
  std::int32_t width{dynamic_objects::kDefaultWidth};
  std::int32_t height{dynamic_objects::kDefaultHeight};
  ItemList::ptr item_list;
  ae::ObjPtr<MainWindowPresenter> presenter;
};

class MainWindowPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::MainWindowPresenter",
                           MainWindowPresenter, Presenter, 0)

 protected:
  MainWindowPresenter() = default;

 public:
  explicit MainWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window);
  }

  MainWindow::ptr window;
};

class Application : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::Application",
                           Application, ae::Obj, 0)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(main_window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, main_window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, main_window);
  }

  MainWindow::ptr main_window;
};

Application::ptr BuildDynamicObjectsGraph(ae::Domain& domain);

// Create Item + presenter with a new ObjId, wire list back-ref, commit.
Item::ptr CommitAddItem(ItemList& list);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DYNAMIC_MODEL_H_
