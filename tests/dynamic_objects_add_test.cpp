#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"

#include "dynamic_ids.h"
#include "dynamic_distill.h"
#include "dynamic_lifecycle.h"
#include "dynamic_model.h"

namespace apptraverse::test {

class TestMainWindowPresenter : public MainWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::dynamic::TestMainWindowPresenter",
      TestMainWindowPresenter, MainWindowPresenter, 0)

 protected:
  TestMainWindowPresenter() = default;

 public:
  explicit TestMainWindowPresenter(ae::ObjProp prop)
      : MainWindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override { ++on_load_calls; }
  void OnModelChanged() override { ++on_model_changed_calls; }

  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_model_changed_calls{0};

  static void ResetCounts() {
    on_load_calls.store(0);
    on_model_changed_calls.store(0);
  }
};

class TestAddItemPresenter : public AddItemPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::dynamic::TestAddItemPresenter",
      TestAddItemPresenter, AddItemPresenter, 0)

 protected:
  TestAddItemPresenter() = default;

 public:
  explicit TestAddItemPresenter(ae::ObjProp prop) : AddItemPresenter{prop} {}

  AE_OBJECT_REFLECT()

  bool ReadyForPresentation() const override {
    return add_item->window->presenter->presentation_loaded;
  }

  void OnLoad() override { ++on_load_calls; }
  void OnModelChanged() override { ++on_model_changed_calls; }

  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_model_changed_calls{0};

  static void ResetCounts() {
    on_load_calls.store(0);
    on_model_changed_calls.store(0);
  }
};

class TestItemListPresenter : public ItemListPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::dynamic::TestItemListPresenter",
      TestItemListPresenter, ItemListPresenter, 0)

 protected:
  TestItemListPresenter() = default;

 public:
  explicit TestItemListPresenter(ae::ObjProp prop)
      : ItemListPresenter{prop} {}

  AE_OBJECT_REFLECT()

  bool ReadyForPresentation() const override {
    return list->window->presenter->presentation_loaded;
  }

  void OnLoad() override { ++on_load_calls; }
  void OnModelChanged() override { ++on_model_changed_calls; }

  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_model_changed_calls{0};

  static void ResetCounts() {
    on_load_calls.store(0);
    on_model_changed_calls.store(0);
  }
};

class TestItemPresenter : public ItemPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::dynamic::TestItemPresenter",
                           TestItemPresenter, ItemPresenter, 0)

 protected:
  TestItemPresenter() = default;

 public:
  explicit TestItemPresenter(ae::ObjProp prop) : ItemPresenter{prop} {}

  AE_OBJECT_REFLECT()

  bool ReadyForPresentation() const override {
    return item->list->presenter->presentation_loaded;
  }

  void OnLoad() override {
    ++on_load_calls;
    last_loaded_number = item->number;
    loaded_ids.push_back(item->obj_id.id());
  }

  void OnModelChanged() override { ++on_model_changed_calls; }

  void OnUnload() override {
    ++on_unload_calls;
    unloaded_ids.push_back(item->obj_id.id());
  }

  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_model_changed_calls{0};
  static inline std::atomic<int> on_unload_calls{0};
  static inline std::atomic<std::uint32_t> last_loaded_number{0};
  static inline std::vector<std::uint32_t> loaded_ids;
  static inline std::vector<std::uint32_t> unloaded_ids;

  static void ResetCounts() {
    on_load_calls.store(0);
    on_model_changed_calls.store(0);
    on_unload_calls.store(0);
    last_loaded_number.store(0);
    loaded_ids.clear();
    unloaded_ids.clear();
  }
};

namespace {

APPTRAVERSE_REGISTER(TestMainWindowPresenter);
APPTRAVERSE_REGISTER(TestAddItemPresenter);
APPTRAVERSE_REGISTER(TestItemListPresenter);
APPTRAVERSE_REGISTER(TestItemPresenter);

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::filesystem::path TestDir(char const* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  return path;
}

void WaitPublished(DynamicModelSession& session) {
  std::unique_lock<std::mutex> lock{session.mu};
  CHECK(session.cv.wait_for(lock, std::chrono::seconds{30}, [&] {
    return session.channel.has_unread_published();
  }));
}

std::vector<std::uint8_t> TakeAndWake(DynamicModelSession& session) {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session.mu};
    bytes = session.channel.TakePublishedCopy();
  }
  session.cv.notify_all();
  CHECK(!bytes.empty());
  return bytes;
}

Application::ptr LoadInitialUi(std::vector<std::uint8_t> const& bytes,
                               ae::Domain& ui_domain,
                               ae::IDomainStorage& ui_storage) {
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto root = LoadInitialPublication(in, ui_domain, ui_storage);
  return Application::ptr::MakeFromThis(static_cast<Application*>(root.get()));
}

ModelObjectProxy MakeSessionProxy(DynamicModelSession& session) {
  return ModelObjectProxy{[&session](ModelObjectProxy::ModelWork work) {
    session.Post(std::move(work));
  }};
}

void PostModelAddClick(DynamicModelSession& session) {
  session.Post([](ae::Domain& domain) {
    auto object = domain.Find(
        ae::ObjId{dynamic_objects::ToObjId(dynamic_objects::ObjId::AddItem)});
    CHECK(object);
    object.as<AddItem>()->Click();
  });
}

void PostModelItemRemove(DynamicModelSession& session, ae::ObjId id) {
  session.Post([id](ae::Domain& domain) {
    auto object = domain.Find(id);
    CHECK(object);
    object.as<Item>()->Remove();
  });
}

void TestReadyForPresentationDependencyOnly() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  auto window = MainWindow::ptr::Create(ae::CreateWith{domain});
  auto main_p = TestMainWindowPresenter::ptr::Create(ae::CreateWith{domain});
  auto add_item = AddItem::ptr::Create(ae::CreateWith{domain});
  auto add_p = TestAddItemPresenter::ptr::Create(ae::CreateWith{domain});
  auto list = ItemList::ptr::Create(ae::CreateWith{domain});
  auto list_p = TestItemListPresenter::ptr::Create(ae::CreateWith{domain});
  auto item = Item::ptr::Create(ae::CreateWith{domain});
  auto item_p = TestItemPresenter::ptr::Create(ae::CreateWith{domain});

  window->presenter = main_p;
  main_p->window = window;
  window->add_item = add_item;
  add_item->window = window;
  add_item->presenter = add_p;
  add_p->add_item = add_item;
  window->item_list = list;
  list->window = window;
  list->presenter = list_p;
  list_p->list = list;
  item->list = list;
  item->presenter = item_p;
  item_p->item = item;
  list->items.push_back(item);

  CHECK(!main_p->presentation_loaded);
  CHECK(!add_p->ReadyForPresentation());
  CHECK(!list_p->ReadyForPresentation());
  CHECK(!item_p->ReadyForPresentation());

  main_p->presentation_loaded = true;
  CHECK(add_p->ReadyForPresentation());
  CHECK(list_p->ReadyForPresentation());
  CHECK(!item_p->ReadyForPresentation());

  list_p->presentation_loaded = true;
  CHECK(item_p->ReadyForPresentation());
}

void TestParentWndProcHasNoSemanticCasts() {
  // Parent windows route via DispatchChildCommand only — no concrete Add/Item
  // presenter names in WndProc implementations.
  std::ifstream in{
      std::filesystem::path{APPTRAVERSE_SOURCE_ROOT} /
      "examples/dynamic_objects_demo/windows/win_presenters.cpp"};
  CHECK(in);
  std::string source((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
  auto const wndproc_main = source.find("Win32MainWindowPresenter::WndProc");
  auto const main_onload = source.find("Win32MainWindowPresenter::OnLoad");
  auto const wndproc_list = source.find("Win32ItemListPresenter::WndProc");
  auto const list_ready =
      source.find("Win32ItemListPresenter::ReadyForPresentation");
  CHECK(wndproc_main != std::string::npos);
  CHECK(main_onload != std::string::npos);
  CHECK(wndproc_list != std::string::npos);
  CHECK(list_ready != std::string::npos);
  CHECK(main_onload > wndproc_main);
  CHECK(list_ready > wndproc_list);
  std::string const main_only =
      source.substr(wndproc_main, main_onload - wndproc_main);
  std::string const list_only =
      source.substr(wndproc_list, list_ready - wndproc_list);
  CHECK(main_only.find("Win32AddItemPresenter") == std::string::npos);
  CHECK(main_only.find("DispatchChildCommand") != std::string::npos);
  CHECK(list_only.find("Win32ItemPresenter") == std::string::npos);
  CHECK(list_only.find("DispatchChildCommand") != std::string::npos);
  CHECK(source.find("dynamic_cast") == std::string::npos);
}

void TestPresenterGraphOwnership() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildDynamicObjectsGraph(domain);
  FinalizeDistilledGraph(*application);

  auto& window = *application->main_window;
  CHECK(window.add_item.is_valid());
  CHECK(window.add_item->presenter.is_valid());
  CHECK(window.presenter.is_valid());
  CHECK(window.add_item->presenter->obj_id != window.presenter->obj_id);
  CHECK(static_cast<ae::Obj*>(&*window.add_item->presenter) !=
        static_cast<ae::Obj*>(&*window.presenter));
  CHECK(&*window.add_item->window == &window);
  CHECK(&*window.add_item->presenter->add_item == &*window.add_item);

  CHECK(window.item_list.is_valid());
  CHECK(window.item_list->items.size() == 1);
  CHECK(window.item_list->items[0]->presenter.is_valid());
  CHECK(window.item_list->items[0]->presenter->obj_id !=
        window.add_item->presenter->obj_id);
}

void TestModelAdd() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildDynamicObjectsGraph(domain);
  FinalizeDistilledGraph(*application);
  ItemList& list = *application->main_window->item_list;
  CHECK(list.items.size() == 1);
  Item* const existing = &*list.items[0];
  auto const existing_id = existing->obj_id;
  auto const journal_before = list.journal.size();

  application->main_window->add_item->Click();

  CHECK(list.journal.size() == journal_before + 1);
  CHECK(list.items.size() == 2);
  CHECK(&*list.items[0] == existing);
  CHECK(list.items[0]->obj_id == existing_id);
  Item* const added = &*list.items[1];
  CHECK(added->obj_id != existing_id);
  CHECK(added->number == 2);
  CHECK(added->presenter.is_valid());
  CHECK(list.journal.back().event->GetClassId() == AddItemEvent::kClassId);
  AddItemEvent::ptr add_event{list.journal.back().event};
  CHECK(&*add_event->item == added);
}

void TestReplayIdentity() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildDynamicObjectsGraph(domain);
  FinalizeDistilledGraph(*application);
  ItemList& list = *application->main_window->item_list;
  application->main_window->add_item->Click();
  auto const added_id = list.items[1]->obj_id;
  auto const event_id = list.journal.back().event->obj_id;
  CHECK(list.items.size() == 2);
  CHECK(list.items[1]->obj_id == added_id);

  list.ReplayFromBase();

  CHECK(list.items.size() == 2);
  CHECK(list.items[0]->number == 1);
  CHECK(list.items[1]->obj_id == added_id);
  CHECK(list.items[1]->number == 2);
  CHECK(list.journal.size() == 1);
  CHECK(list.journal.back().event->obj_id == event_id);
  AddItemEvent::ptr add_event{list.journal.back().event};
  CHECK(&*add_event->item == &*list.items[1]);
}

void TestStructuralPublicationAndPresenterLifecycle() {
  TestMainWindowPresenter::ResetCounts();
  TestAddItemPresenter::ResetCounts();
  TestItemListPresenter::ResetCounts();
  TestItemPresenter::ResetCounts();

  auto dir = TestDir("apptraverse_dynamic_structural");
  DynamicModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);

  Application* const app = &*ui_app;
  MainWindow* const window = &*ui_app->main_window;
  AddItem* const add_item = &*window->add_item;
  Presenter* const add_p = &*add_item->presenter;
  ItemList* const list = &*ui_app->main_window->item_list;
  Item* const item1 = &*list->items[0];
  Presenter* const main_p = &*window->presenter;
  Presenter* const list_p = &*list->presenter;
  Presenter* const item1_p = &*item1->presenter;
  auto const item1_id = item1->obj_id;

  CHECK(list->items.size() == 1);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);
  CHECK(TestAddItemPresenter::on_load_calls.load() == 1);
  CHECK(TestItemListPresenter::on_load_calls.load() == 1);
  CHECK(TestItemPresenter::on_load_calls.load() == 1);
  CHECK(main_p->presentation_load_order < add_p->presentation_load_order);
  CHECK(main_p->presentation_load_order < list_p->presentation_load_order);
  CHECK(list_p->presentation_load_order < item1_p->presentation_load_order);

  PostModelAddClick(session);
  WaitPublished(session);
  auto bytes = TakeAndWake(session);

  ApplyItemListStructural(bytes, *ui_app, ui_storage, nullptr, &proxy);

  CHECK(&*ui_app == app);
  CHECK(&*ui_app->main_window == window);
  CHECK(&*window->add_item == add_item);
  CHECK(&*add_item->presenter == add_p);
  CHECK(&*ui_app->main_window->item_list == list);
  CHECK(&*list->items[0] == item1);
  CHECK(list->items[0]->obj_id == item1_id);
  CHECK(&*window->presenter == main_p);
  CHECK(&*list->presenter == list_p);
  CHECK(&*item1->presenter == item1_p);
  CHECK(list->items.size() == 2);

  Item* const item2 = &*list->items[1];
  CHECK(item2 != item1);
  CHECK(item2->number == 2);
  CHECK(item2->obj_id != item1_id);
  CHECK(item2->presenter.is_valid());
  CHECK(&*item2->presenter != item1_p);

  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);
  CHECK(TestAddItemPresenter::on_load_calls.load() == 1);
  CHECK(TestItemListPresenter::on_load_calls.load() == 1);
  CHECK(TestItemPresenter::on_load_calls.load() == 2);
  CHECK(TestItemPresenter::last_loaded_number.load() == 2);

  session.RequestStop();
  model.join();

  DirectoryDomainStorage model_storage{dir};
  ae::Domain model_domain{model_storage};
  auto model_app = LoadApplication<Application>(
      model_domain, ae::ObjId{dynamic_objects::ToObjId(
                        dynamic_objects::ObjId::Application)});
  CHECK(model_app->main_window->item_list->items.size() == 2);
  auto const model_item2_id =
      model_app->main_window->item_list->items[1]->obj_id;
  CHECK(item2->obj_id == model_item2_id);
  CHECK(item2 != &*model_app->main_window->item_list->items[1]);

  std::filesystem::remove_all(dir);
}

void TestGuiProxyAddAndRemove() {
  TestItemPresenter::ResetCounts();
  auto dir = TestDir("apptraverse_dynamic_gui_proxy");
  DynamicModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);

  AddItem* const ui_add = &*ui_app->main_window->add_item;
  auto const add_id = ui_add->obj_id;
  CHECK(ui_add->presenter.is_valid());
  CHECK(ui_add->presenter->model_proxy == &proxy);

  ae::RamDomainStorage model_ref_storage;
  ae::Domain model_ref_domain{model_ref_storage};
  auto model_ref = BuildDynamicObjectsGraph(model_ref_domain);
  AddItem* const model_add = &*model_ref->main_window->add_item;
  CHECK(model_add->obj_id == add_id);
  CHECK(model_add != ui_add);

  ui_add->presenter->Click();
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->main_window->item_list->items.size() == 2);
  Item* const ui_item2 = &*ui_app->main_window->item_list->items[1];
  CHECK(ui_item2->number == 2);
  CHECK(ui_item2->presenter->model_proxy == &proxy);
  auto const item2_id = ui_item2->obj_id;
  CHECK(TestItemPresenter::on_load_calls.load() == 2);

  ui_item2->presenter->RemoveClick();
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->main_window->item_list->items.size() == 1);
  CHECK(TestItemPresenter::on_unload_calls.load() == 1);
  CHECK(TestItemPresenter::unloaded_ids[0] == item2_id.id());

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

void TestTwoAddsAreTwoEvents() {
  TestItemPresenter::ResetCounts();
  auto dir = TestDir("apptraverse_dynamic_two_adds");
  DynamicModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);
  Item* const item1 = &*ui_app->main_window->item_list->items[0];
  CHECK(TestItemPresenter::on_load_calls.load() == 1);

  PostModelAddClick(session);
  PostModelAddClick(session);

  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->main_window->item_list->items.size() == 2);
  Item* const item2 = &*ui_app->main_window->item_list->items[1];
  CHECK(&*ui_app->main_window->item_list->items[0] == item1);
  CHECK(TestItemPresenter::on_load_calls.load() == 2);

  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->main_window->item_list->items.size() == 3);
  Item* const item3 = &*ui_app->main_window->item_list->items[2];
  CHECK(&*ui_app->main_window->item_list->items[0] == item1);
  CHECK(&*ui_app->main_window->item_list->items[1] == item2);
  CHECK(item3 != item2);
  CHECK(item3->obj_id != item2->obj_id);
  CHECK(item2->obj_id != item1->obj_id);
  CHECK(TestItemPresenter::on_load_calls.load() == 3);

  session.RequestStop();
  model.join();

  DirectoryDomainStorage model_storage{dir};
  ae::Domain model_domain{model_storage};
  auto model_app = LoadApplication<Application>(
      model_domain, ae::ObjId{dynamic_objects::ToObjId(
                        dynamic_objects::ObjId::Application)});
  auto& model_list = *model_app->main_window->item_list;
  CHECK(model_list.items.size() == 3);
  CHECK(model_list.journal.size() == 2);
  CHECK(model_list.items[1]->obj_id == item2->obj_id);
  CHECK(model_list.items[2]->obj_id == item3->obj_id);

  std::filesystem::remove_all(dir);
}

void TestRestartRestoresAddedItem() {
  auto dir = TestDir("apptraverse_dynamic_restart");
  {
    DynamicModelSession session;
    session.state_dir = dir;
    std::thread model{[&] {
      session.Run([&session](PublicationKind) { session.cv.notify_all(); });
    }};
    WaitPublished(session);
    TakeAndWake(session);
    PostModelAddClick(session);
    WaitPublished(session);
    TakeAndWake(session);
    session.RequestStop();
    model.join();
  }

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{dynamic_objects::ToObjId(
                  dynamic_objects::ObjId::Application)});
  CHECK(application->main_window->item_list->items.size() == 2);
  CHECK(application->main_window->item_list->items[0]->number == 1);
  CHECK(application->main_window->item_list->items[1]->number == 2);
  CHECK(application->main_window->item_list->journal.size() == 1);
  CHECK(application->main_window->add_item.is_valid());

  std::filesystem::remove_all(dir);
}

void TestModelRemove() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildDynamicObjectsGraph(domain);
  FinalizeDistilledGraph(*application);
  ItemList& list = *application->main_window->item_list;
  Item* const item1 = &*list.items[0];
  application->main_window->add_item->Click();
  Item::ptr item2 = list.items[1];
  auto const item2_id = item2->obj_id;
  CHECK(list.items.size() == 2);
  CHECK(list.journal.size() == 1);

  item2->Remove();
  CHECK(list.items.size() == 1);
  CHECK(&*list.items[0] == item1);
  CHECK(list.journal.size() == 2);
  CHECK(list.journal.back().event->GetClassId() == RemoveItemEvent::kClassId);
  RemoveItemEvent::ptr remove_event{list.journal.back().event};
  CHECK(remove_event->item->obj_id == item2_id);
  AddItemEvent::ptr add_event{list.journal.front().event};
  CHECK(add_event->item->obj_id == item2_id);
  item2->Remove();  // stale no-op
  CHECK(list.journal.size() == 2);
}

void TestReplayRemove() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildDynamicObjectsGraph(domain);
  FinalizeDistilledGraph(*application);
  ItemList& list = *application->main_window->item_list;
  application->main_window->add_item->Click();
  Item::ptr item2 = list.items[1];
  auto const item2_id = item2->obj_id;
  auto const add_event_id = list.journal[0].event->obj_id;
  item2->Remove();
  auto const remove_event_id = list.journal[1].event->obj_id;
  CHECK(list.items.size() == 1);

  list.ReplayFromBase();

  CHECK(list.items.size() == 1);
  CHECK(list.items[0]->number == 1);
  CHECK(list.journal.size() == 2);
  CHECK(list.journal[0].event->obj_id == add_event_id);
  CHECK(list.journal[1].event->obj_id == remove_event_id);
  AddItemEvent::ptr add_event{list.journal[0].event};
  CHECK(add_event->item->obj_id == item2_id);
  for (auto const& item : list.items) {
    CHECK(item->obj_id != item2_id);
  }
}

void TestMirrorRemoveAndHistoricalPresenter() {
  TestItemPresenter::ResetCounts();
  auto dir = TestDir("apptraverse_dynamic_mirror_remove");
  DynamicModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);
  Application* const app = &*ui_app;
  MainWindow* const window = &*ui_app->main_window;
  ItemList* const list = &*ui_app->main_window->item_list;
  Item* const item1 = &*list->items[0];
  Presenter* const item1_p = &*item1->presenter;
  CHECK(TestItemPresenter::on_load_calls.load() == 1);

  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(list->items.size() == 2);
  Item::ptr item2_hold = list->items[1];
  ItemPresenter::ptr item2_presenter_hold = item2_hold->presenter;
  auto const item2_id = item2_hold->obj_id;
  CHECK(TestItemPresenter::on_load_calls.load() == 2);
  CHECK(TestItemPresenter::on_unload_calls.load() == 0);

  PostModelItemRemove(session, item2_id);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);

  CHECK(&*ui_app == app);
  CHECK(&*ui_app->main_window == window);
  CHECK(&*ui_app->main_window->item_list == list);
  CHECK(list->items.size() == 1);
  CHECK(&*list->items[0] == item1);
  CHECK(&*item1->presenter == item1_p);
  CHECK(TestItemPresenter::on_load_calls.load() == 2);
  CHECK(TestItemPresenter::on_unload_calls.load() == 1);
  CHECK(TestItemPresenter::unloaded_ids.size() == 1);
  CHECK(TestItemPresenter::unloaded_ids[0] == item2_id.id());
  CHECK(!item2_presenter_hold->presentation_loaded);

  InitializeNewPresenters(*ui_app, nullptr, &proxy);
  CHECK(TestItemPresenter::on_load_calls.load() == 2);
  CHECK(TestItemPresenter::on_unload_calls.load() == 1);

  std::vector<ae::Obj*> live;
  CollectLiveReachableObjects(*ui_app, live);
  for (ae::Obj* obj : live) {
    CHECK(obj->obj_id != item2_id);
  }

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

void TestMiddleRemoveThenAdd() {
  TestItemPresenter::ResetCounts();
  auto dir = TestDir("apptraverse_dynamic_middle_remove");
  DynamicModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);
  Item* const item1 = &*ui_app->main_window->item_list->items[0];
  Presenter* const item1_p = &*item1->presenter;

  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->main_window->item_list->items.size() == 3);
  Item* const item2 = &*ui_app->main_window->item_list->items[1];
  Item* const item3 = &*ui_app->main_window->item_list->items[2];
  Presenter* const item3_p = &*item3->presenter;
  auto const item2_id = item2->obj_id;
  auto const item3_id = item3->obj_id;
  CHECK(item1->number == 1);
  CHECK(item2->number == 2);
  CHECK(item3->number == 3);

  PostModelItemRemove(session, item2_id);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->main_window->item_list->items.size() == 2);
  CHECK(&*ui_app->main_window->item_list->items[0] == item1);
  CHECK(&*ui_app->main_window->item_list->items[1] == item3);
  CHECK(&*item1->presenter == item1_p);
  CHECK(&*item3->presenter == item3_p);
  CHECK(TestItemPresenter::on_unload_calls.load() == 1);

  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->main_window->item_list->items.size() == 3);
  Item* const item4 = &*ui_app->main_window->item_list->items[2];
  CHECK(item4->number == 4);
  CHECK(item4->obj_id != item2_id);
  CHECK(item4->obj_id != item3_id);
  CHECK(ui_app->main_window->item_list->items[0]->number == 1);
  CHECK(ui_app->main_window->item_list->items[1]->number == 3);
  CHECK(&*item1->presenter == item1_p);
  CHECK(&*item3->presenter == item3_p);

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

void TestMultiOperationSequence() {
  auto dir = TestDir("apptraverse_dynamic_sequence");
  DynamicModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);
  auto* list = &*ui_app->main_window->item_list;
  auto const item1_id = list->items[0]->obj_id;

  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  auto const item2_id = list->items[1]->obj_id;
  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  auto const item3_id = list->items[2]->obj_id;
  PostModelItemRemove(session, item2_id);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  auto const item4_id = list->items[2]->obj_id;
  PostModelItemRemove(session, item1_id);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);

  CHECK(list->items.size() == 2);
  CHECK(list->items[0]->obj_id == item3_id);
  CHECK(list->items[1]->obj_id == item4_id);
  CHECK(list->items[0]->number == 3);
  CHECK(list->items[1]->number == 4);

  session.RequestStop();
  model.join();

  DirectoryDomainStorage model_storage{dir};
  ae::Domain model_domain{model_storage};
  auto model_app = LoadApplication<Application>(
      model_domain, ae::ObjId{dynamic_objects::ToObjId(
                        dynamic_objects::ObjId::Application)});
  auto& model_list = *model_app->main_window->item_list;
  CHECK(model_list.items.size() == 2);
  CHECK(model_list.items[0]->obj_id == item3_id);
  CHECK(model_list.items[1]->obj_id == item4_id);
  CHECK(model_list.journal.size() == 5);

  std::filesystem::remove_all(dir);
}

void TestShutdownDrainsAcceptedAdds() {
  auto dir = TestDir("apptraverse_dynamic_shutdown_drain_add");
  DynamicModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  {
    std::lock_guard<std::mutex> lock{session.mu};
    (void)session.channel.TakePublishedCopy();
  }
  session.cv.notify_all();

  PostModelAddClick(session);
  PostModelAddClick(session);
  session.RequestStop();
  model.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{dynamic_objects::ToObjId(
                  dynamic_objects::ObjId::Application)});
  auto& list = *application->main_window->item_list;
  CHECK(list.items.size() == 3);
  CHECK(list.items[0]->number == 1);
  CHECK(list.items[1]->number == 2);
  CHECK(list.items[2]->number == 3);
  CHECK(list.journal.size() == 2);
  std::filesystem::remove_all(dir);
}

void TestShutdownDrainsAcceptedRemove() {
  auto dir = TestDir("apptraverse_dynamic_shutdown_drain_remove");
  DynamicModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);

  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  PostModelAddClick(session);
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  auto const item2_id = ui_app->main_window->item_list->items[1]->obj_id;

  PostModelItemRemove(session, item2_id);
  session.RequestStop();
  model.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{dynamic_objects::ToObjId(
                  dynamic_objects::ObjId::Application)});
  auto& list = *application->main_window->item_list;
  CHECK(list.items.size() == 2);
  CHECK(list.items[0]->number == 1);
  CHECK(list.items[1]->number == 3);
  CHECK(list.journal.size() == 3);

  session.Post([](ae::Domain&) {});
  CHECK(session.pending_work.empty());
  std::filesystem::remove_all(dir);
}

}  // namespace

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestReadyForPresentationDependencyOnly();
  apptraverse::test::TestParentWndProcHasNoSemanticCasts();
  apptraverse::test::TestPresenterGraphOwnership();
  apptraverse::test::TestModelAdd();
  apptraverse::test::TestReplayIdentity();
  apptraverse::test::TestStructuralPublicationAndPresenterLifecycle();
  apptraverse::test::TestGuiProxyAddAndRemove();
  apptraverse::test::TestTwoAddsAreTwoEvents();
  apptraverse::test::TestRestartRestoresAddedItem();
  apptraverse::test::TestModelRemove();
  apptraverse::test::TestReplayRemove();
  apptraverse::test::TestMirrorRemoveAndHistoricalPresenter();
  apptraverse::test::TestMiddleRemoveThenAdd();
  apptraverse::test::TestMultiOperationSequence();
  apptraverse::test::TestShutdownDrainsAcceptedAdds();
  apptraverse::test::TestShutdownDrainsAcceptedRemove();
  std::cout << "dynamic_objects_add_test OK\n";
  return 0;
}
