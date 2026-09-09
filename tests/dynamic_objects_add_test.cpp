#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"

#include "dynamic_ids.h"
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
    if (!list.is_valid() || !list.is_loaded() || !list->domain) {
      return false;
    }
    auto window = list->domain->Find(ae::ObjId{dynamic_objects::ToObjId(
        dynamic_objects::ObjId::MainWindow)});
    if (!window) {
      return false;
    }
    auto* main = dynamic_cast<MainWindow*>(&*window);
    return main != nullptr && main->presenter.is_valid() &&
           main->presenter.is_loaded() &&
           main->presenter->presentation_loaded;
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
    return item.is_valid() && item.is_loaded() && item->list.is_valid() &&
           item->list.is_loaded() && item->list->presenter.is_valid() &&
           item->list->presenter.is_loaded() &&
           item->list->presenter->presentation_loaded;
  }

  void OnLoad() override {
    ++on_load_calls;
    if (item.is_valid()) {
      last_loaded_number = item->number;
      loaded_ids.push_back(item->obj_id.id());
    }
  }

  void OnModelChanged() override { ++on_model_changed_calls; }

  void OnUnload() override {
    ++on_unload_calls;
    if (item.is_valid()) {
      unloaded_ids.push_back(item->obj_id.id());
    }
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

  Item::ptr added = CommitAddItem(list);

  CHECK(list.journal.size() == journal_before + 1);
  CHECK(list.items.size() == 2);
  CHECK(&*list.items[0] == existing);
  CHECK(list.items[0]->obj_id == existing_id);
  CHECK(&*list.items[1] == &*added);
  CHECK(added->obj_id != existing_id);
  CHECK(added->number == 2);
  CHECK(dynamic_cast<AddItemEvent*>(&*list.journal.back().event) != nullptr);
  CHECK(&*dynamic_cast<AddItemEvent&>(*list.journal.back().event).item ==
        &*added);
}

void TestReplayIdentity() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildDynamicObjectsGraph(domain);
  FinalizeDistilledGraph(*application);
  ItemList& list = *application->main_window->item_list;
  Item::ptr added = CommitAddItem(list);
  auto const added_id = added->obj_id;
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
  CHECK(&*dynamic_cast<AddItemEvent&>(*list.journal.back().event).item ==
        &*list.items[1]);
}

void TestStructuralPublicationAndPresenterLifecycle() {
  TestMainWindowPresenter::ResetCounts();
  TestItemListPresenter::ResetCounts();
  TestItemPresenter::ResetCounts();

  auto dir = TestDir("apptraverse_dynamic_structural");
  DynamicModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app);

  Application* const app = &*ui_app;
  MainWindow* const window = &*ui_app->main_window;
  ItemList* const list = &*ui_app->main_window->item_list;
  Item* const item1 = &*list->items[0];
  Presenter* const main_p = &*window->presenter;
  Presenter* const list_p = &*list->presenter;
  Presenter* const item1_p = &*item1->presenter;
  auto const item1_id = item1->obj_id;

  CHECK(list->items.size() == 1);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);
  CHECK(TestItemListPresenter::on_load_calls.load() == 1);
  CHECK(TestItemPresenter::on_load_calls.load() == 1);

  session.SubmitAddItem(AddItemCommand{1});
  WaitPublished(session);
  auto bytes = TakeAndWake(session);

  // Peek model identity via a separate load of the same state dir after stop
  // is not available yet — capture model ObjId from publication apply only
  // after apply. For ObjId equality, reopen model storage after stop.
  ApplyItemListStructural(bytes, *ui_app, ui_storage, nullptr);

  CHECK(&*ui_app == app);
  CHECK(&*ui_app->main_window == window);
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

void TestTwoAddsAreTwoEvents() {
  TestItemPresenter::ResetCounts();
  auto dir = TestDir("apptraverse_dynamic_two_adds");
  DynamicModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app);
  Item* const item1 = &*ui_app->main_window->item_list->items[0];
  CHECK(TestItemPresenter::on_load_calls.load() == 1);

  session.SubmitAddItem(AddItemCommand{1});
  session.SubmitAddItem(AddItemCommand{2});

  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  CHECK(ui_app->main_window->item_list->items.size() == 2);
  Item* const item2 = &*ui_app->main_window->item_list->items[1];
  CHECK(&*ui_app->main_window->item_list->items[0] == item1);
  CHECK(TestItemPresenter::on_load_calls.load() == 2);

  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
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
    session.SubmitAddItem(AddItemCommand{1});
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

  std::filesystem::remove_all(dir);
}

void TestModelRemove() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildDynamicObjectsGraph(domain);
  FinalizeDistilledGraph(*application);
  ItemList& list = *application->main_window->item_list;
  Item* const item1 = &*list.items[0];
  Item::ptr item2 = CommitAddItem(list);
  auto const item2_id = item2->obj_id;
  CHECK(list.items.size() == 2);
  CHECK(list.journal.size() == 1);

  CHECK(CommitRemoveItem(list, item2_id));
  CHECK(list.items.size() == 1);
  CHECK(&*list.items[0] == item1);
  CHECK(list.journal.size() == 2);
  CHECK(dynamic_cast<RemoveItemEvent*>(&*list.journal.back().event) != nullptr);
  CHECK(dynamic_cast<RemoveItemEvent&>(*list.journal.back().event)
            .item->obj_id == item2_id);
  // Historical AddItemEvent still holds Item2.
  CHECK(dynamic_cast<AddItemEvent&>(*list.journal.front().event).item->obj_id ==
        item2_id);
  CHECK(!CommitRemoveItem(list, item2_id));  // stale no-op
  CHECK(list.journal.size() == 2);
}

void TestReplayRemove() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildDynamicObjectsGraph(domain);
  FinalizeDistilledGraph(*application);
  ItemList& list = *application->main_window->item_list;
  Item::ptr item2 = CommitAddItem(list);
  auto const item2_id = item2->obj_id;
  auto const add_event_id = list.journal[0].event->obj_id;
  CHECK(CommitRemoveItem(list, item2_id));
  auto const remove_event_id = list.journal[1].event->obj_id;
  CHECK(list.items.size() == 1);

  list.ReplayFromBase();

  CHECK(list.items.size() == 1);
  CHECK(list.items[0]->number == 1);
  CHECK(list.journal.size() == 2);
  CHECK(list.journal[0].event->obj_id == add_event_id);
  CHECK(list.journal[1].event->obj_id == remove_event_id);
  CHECK(dynamic_cast<AddItemEvent&>(*list.journal[0].event).item->obj_id ==
        item2_id);
  for (auto const& item : list.items) {
    CHECK(item->obj_id != item2_id);
  }
}

void TestMirrorRemoveAndHistoricalPresenter() {
  TestItemPresenter::ResetCounts();
  auto dir = TestDir("apptraverse_dynamic_mirror_remove");
  DynamicModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app);
  Application* const app = &*ui_app;
  MainWindow* const window = &*ui_app->main_window;
  ItemList* const list = &*ui_app->main_window->item_list;
  Item* const item1 = &*list->items[0];
  Presenter* const item1_p = &*item1->presenter;
  CHECK(TestItemPresenter::on_load_calls.load() == 1);

  session.SubmitAddItem(AddItemCommand{1});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  CHECK(list->items.size() == 2);
  Item::ptr item2_hold = list->items[1];
  ItemPresenter::ptr item2_presenter_hold = item2_hold->presenter;
  auto const item2_id = item2_hold->obj_id;
  CHECK(TestItemPresenter::on_load_calls.load() == 2);
  CHECK(TestItemPresenter::on_unload_calls.load() == 0);

  session.SubmitRemoveItem(RemoveItemCommand{item2_id});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);

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

  // Historical objects must not reactivate through live presenter discovery.
  InitializeNewPresenters(*ui_app, nullptr);
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
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app);
  Item* const item1 = &*ui_app->main_window->item_list->items[0];
  Presenter* const item1_p = &*item1->presenter;

  session.SubmitAddItem(AddItemCommand{1});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  session.SubmitAddItem(AddItemCommand{2});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  CHECK(ui_app->main_window->item_list->items.size() == 3);
  Item* const item2 = &*ui_app->main_window->item_list->items[1];
  Item* const item3 = &*ui_app->main_window->item_list->items[2];
  Presenter* const item3_p = &*item3->presenter;
  auto const item2_id = item2->obj_id;
  auto const item3_id = item3->obj_id;
  CHECK(item1->number == 1);
  CHECK(item2->number == 2);
  CHECK(item3->number == 3);

  session.SubmitRemoveItem(RemoveItemCommand{item2_id});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  CHECK(ui_app->main_window->item_list->items.size() == 2);
  CHECK(&*ui_app->main_window->item_list->items[0] == item1);
  CHECK(&*ui_app->main_window->item_list->items[1] == item3);
  CHECK(&*item1->presenter == item1_p);
  CHECK(&*item3->presenter == item3_p);
  CHECK(TestItemPresenter::on_unload_calls.load() == 1);

  session.SubmitAddItem(AddItemCommand{3});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
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
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app);
  auto* list = &*ui_app->main_window->item_list;
  auto const item1_id = list->items[0]->obj_id;

  session.SubmitAddItem(AddItemCommand{1});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  auto const item2_id = list->items[1]->obj_id;
  session.SubmitAddItem(AddItemCommand{2});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  auto const item3_id = list->items[2]->obj_id;
  session.SubmitRemoveItem(RemoveItemCommand{item2_id});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  session.SubmitAddItem(AddItemCommand{3});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);
  auto const item4_id = list->items[2]->obj_id;
  session.SubmitRemoveItem(RemoveItemCommand{item1_id});
  WaitPublished(session);
  ApplyItemListStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr);

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

}  // namespace

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestModelAdd();
  apptraverse::test::TestReplayIdentity();
  apptraverse::test::TestStructuralPublicationAndPresenterLifecycle();
  apptraverse::test::TestTwoAddsAreTwoEvents();
  apptraverse::test::TestRestartRestoresAddedItem();
  apptraverse::test::TestModelRemove();
  apptraverse::test::TestReplayRemove();
  apptraverse::test::TestMirrorRemoveAndHistoricalPresenter();
  apptraverse::test::TestMiddleRemoveThenAdd();
  apptraverse::test::TestMultiOperationSequence();
  std::cout << "dynamic_objects_add_test OK\n";
  return 0;
}
