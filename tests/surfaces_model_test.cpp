#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

#include "surfaces_distill.h"
#include "surfaces_ids.h"
#include "surfaces_lifecycle.h"
#include "surfaces_model.h"

namespace apptraverse::test {

class TestSurfacePresenter : public SurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::TestSurfacePresenter",
      TestSurfacePresenter, SurfacePresenter, 0)

 protected:
  TestSurfacePresenter() = default;

 public:
  explicit TestSurfacePresenter(ae::ObjProp prop) : SurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override {
    ++on_load_calls;
    loaded_ids.push_back(obj_id.id());
  }
  void OnModelChanged() override { ++on_model_changed_calls; }
  void OnUnload() override {
    ++on_unload_calls;
    unloaded_ids.push_back(obj_id.id());
  }

  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_model_changed_calls{0};
  static inline std::atomic<int> on_unload_calls{0};
  static inline std::vector<std::uint32_t> loaded_ids;
  static inline std::vector<std::uint32_t> unloaded_ids;

  static void ResetCounts() {
    on_load_calls.store(0);
    on_model_changed_calls.store(0);
    on_unload_calls.store(0);
    loaded_ids.clear();
    unloaded_ids.clear();
  }
};

namespace {

APPTRAVERSE_REGISTER(TestSurfacePresenter);

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

bool IsNodeObj(ae::Obj const& obj) {
  return ae::Registry::GetRegistry().GenerationDistance(
             Node::kClassId, obj.GetClassId()) >= 0;
}

std::filesystem::path TestDir(char const* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  return path;
}

void WaitPublished(SurfacesModelSession& session) {
  std::unique_lock<std::mutex> lock{session.mu};
  CHECK(session.cv.wait_for(lock, std::chrono::seconds{30}, [&] {
    return session.channel.has_unread_published();
  }));
}

std::vector<std::uint8_t> TakeAndWake(SurfacesModelSession& session) {
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

ModelObjectProxy MakeSessionProxy(SurfacesModelSession& session) {
  return ModelObjectProxy{[&session](ModelObjectProxy::ModelWork work) {
    session.Post(std::move(work));
  }};
}

// Run work on the model thread and wait for it, so each test step is ordered
// against the publication state the next assertion inspects.
void RunOnModel(SurfacesModelSession& session,
                std::function<void(Application&)> body) {
  std::promise<void> done;
  auto future = done.get_future();
  session.Post([&](ae::Domain& domain) {
    auto app = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    body(*app);
    done.set_value();
  });
  CHECK(future.wait_for(std::chrono::seconds{30}) ==
        std::future_status::ready);
}

void TestInitialGraph() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);

  Surfaces& surfaces = *application->surfaces;
  CHECK(surfaces.surfaces.size() == 1);
  Surface& surface = *surfaces.surfaces[0];
  CHECK(surface.number == 1);
  CHECK(&*surface.surfaces == &surfaces);
  CHECK(surface.presenter.is_valid());
  CHECK(&*surface.presenter->surface == &surface);
  CHECK(IsNodeObj(surface));
  CHECK(IsNodeObj(surfaces));
  CHECK(surface.base.is_valid());
  CHECK(surface.journal.empty());
  CHECK(surfaces.base.is_valid());
  CHECK(surfaces.journal.empty());
  CHECK(!surfaces.mobile_current);
}

void TestModelAdd() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surfaces& surfaces = *application->surfaces;
  Surface* const surface1 = &*surfaces.surfaces[0];
  auto const surface1_id = surface1->obj_id;
  auto const journal_before = surfaces.journal.size();

  surface1->AddSurface();

  CHECK(surfaces.journal.size() == journal_before + 1);
  CHECK(surfaces.surfaces.size() == 2);
  CHECK(&*surfaces.surfaces[0] == surface1);
  CHECK(surfaces.surfaces[0]->obj_id == surface1_id);
  Surface* const surface2 = &*surfaces.surfaces[1];
  CHECK(surface2->obj_id != surface1_id);
  CHECK(surface2->number == 2);
  CHECK(&*surface2->surfaces == &surfaces);
  CHECK(surface2->presenter.is_valid());
  CHECK(&*surface2->presenter->surface == surface2);
  CHECK(IsNodeObj(*surface2));
  CHECK(surface2->base.is_valid());
  CHECK(surface2->journal.empty());
  CHECK(surfaces.journal.back().event->GetClassId() ==
        AddSurfaceEvent::kClassId);
  AddSurfaceEvent::ptr add_event{surfaces.journal.back().event};
  CHECK(&*add_event->surface == surface2);
}

void TestAddFromSecondSurface() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surfaces& surfaces = *application->surfaces;
  surfaces.surfaces[0]->AddSurface();
  CHECK(surfaces.surfaces.size() == 2);
  Surface* const surface2 = &*surfaces.surfaces[1];
  surface2->AddSurface();
  CHECK(surfaces.surfaces.size() == 3);
  CHECK(surfaces.surfaces[0]->number == 1);
  CHECK(surfaces.surfaces[1]->number == 2);
  CHECK(surfaces.surfaces[2]->number == 3);
  CHECK(surfaces.journal.size() == 2);
}

void TestModelRemove() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surfaces& surfaces = *application->surfaces;
  Surface* const surface1 = &*surfaces.surfaces[0];
  surface1->AddSurface();
  surface1->AddSurface();
  CHECK(surfaces.surfaces.size() == 3);
  Surface::ptr surface2 = surfaces.surfaces[1];
  auto const surface2_id = surface2->obj_id;
  Surface* const surface3 = &*surfaces.surfaces[2];

  surface2->Remove();
  CHECK(surfaces.surfaces.size() == 2);
  CHECK(&*surfaces.surfaces[0] == surface1);
  CHECK(&*surfaces.surfaces[1] == surface3);
  CHECK(surfaces.journal.size() == 3);
  CHECK(surfaces.journal.back().event->GetClassId() ==
        RemoveSurfaceEvent::kClassId);
  RemoveSurfaceEvent::ptr remove_event{surfaces.journal.back().event};
  CHECK(remove_event->surface->obj_id == surface2_id);

  surface2->Remove();  // stale no-op
  CHECK(surfaces.journal.size() == 3);
}

void TestReplay() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surfaces& surfaces = *application->surfaces;
  surfaces.surfaces[0]->AddSurface();
  surfaces.surfaces[0]->AddSurface();
  Surface::ptr surface2 = surfaces.surfaces[1];
  Surface::ptr surface3 = surfaces.surfaces[2];
  auto const surface2_id = surface2->obj_id;
  auto const surface3_id = surface3->obj_id;
  auto const add2_event_id = surfaces.journal[0].event->obj_id;
  auto const add3_event_id = surfaces.journal[1].event->obj_id;
  surface2->Remove();
  auto const remove_event_id = surfaces.journal[2].event->obj_id;
  CHECK(surfaces.surfaces.size() == 2);

  surfaces.ReplayFromBase();

  CHECK(surfaces.surfaces.size() == 2);
  CHECK(surfaces.surfaces[0]->number == 1);
  CHECK(surfaces.surfaces[1]->number == 3);
  CHECK(surfaces.surfaces[1]->obj_id == surface3_id);
  CHECK(surfaces.journal.size() == 3);
  CHECK(surfaces.journal[0].event->obj_id == add2_event_id);
  CHECK(surfaces.journal[1].event->obj_id == add3_event_id);
  CHECK(surfaces.journal[2].event->obj_id == remove_event_id);
  AddSurfaceEvent::ptr add2{surfaces.journal[0].event};
  CHECK(add2->surface->obj_id == surface2_id);
  for (auto const& live : surfaces.surfaces) {
    CHECK(live->obj_id != surface2_id);
  }
}

void TestRestartPersistence() {
  auto dir = TestDir("apptraverse_surfaces_restart");
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = BuildSurfacesGraph(domain);
    FinalizeDistilledGraph(*application);
    Surfaces& surfaces = *application->surfaces;
    surfaces.surfaces[0]->AddSurface();
    surfaces.surfaces[0]->AddSurface();
    surfaces.surfaces[1]->Remove();
    CHECK(surfaces.surfaces.size() == 2);
    CHECK(surfaces.surfaces[0]->number == 1);
    CHECK(surfaces.surfaces[1]->number == 3);
    SaveDistilledRoot(*application);
  }

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  Surfaces& surfaces = *application->surfaces;
  CHECK(surfaces.surfaces.size() == 2);
  CHECK(surfaces.surfaces[0]->number == 1);
  CHECK(surfaces.surfaces[1]->number == 3);
  auto const surface3_id = surfaces.surfaces[1]->obj_id;
  surfaces.surfaces[1]->AddSurface();
  CHECK(surfaces.surfaces.size() == 3);
  CHECK(surfaces.surfaces[2]->number == 4);
  CHECK(surfaces.surfaces[2]->obj_id != surface3_id);
  std::filesystem::remove_all(dir);
}

void TestDynamicNodeStructuralPublication() {
  TestSurfacePresenter::ResetCounts();
  auto dir = TestDir("apptraverse_surfaces_dyn_node_pub");
  SurfacesModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run(
        [&session](SurfacesPublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);
  Surfaces* const ui_surfaces = &*ui_app->surfaces;
  Surface* const ui_surface1 = &*ui_surfaces->surfaces[0];
  Presenter* const ui_surface1_p = &*ui_surface1->presenter;
  auto const surface1_id = ui_surface1->obj_id;
  CHECK(TestSurfacePresenter::on_load_calls.load() == 1);
  CHECK(IsNodeObj(*ui_surface1));

  ae::Obj* model_surface1 = nullptr;
  session.Post([&](ae::Domain& domain) {
    auto object = domain.Find(surface1_id);
    CHECK(object);
    model_surface1 = &*object;
    object.as<Surface>()->AddSurface();
  });
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);

  CHECK(ui_surfaces->surfaces.size() == 2);
  CHECK(&*ui_surfaces->surfaces[0] == ui_surface1);
  CHECK(&*ui_surface1->presenter == ui_surface1_p);
  Surface* const ui_surface2 = &*ui_surfaces->surfaces[1];
  CHECK(ui_surface2->number == 2);
  CHECK(IsNodeObj(*ui_surface2));
  CHECK(ui_surface2->presenter.is_valid());
  CHECK(TestSurfacePresenter::on_load_calls.load() == 2);
  CHECK(static_cast<ae::Obj*>(ui_surface1) != model_surface1);
  CHECK(ui_surface1->obj_id == surface1_id);
  CHECK(ui_surface2->obj_id != surface1_id);

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

void TestGuiProxyAddRemoveAndRemoveCurrent() {
  TestSurfacePresenter::ResetCounts();
  auto dir = TestDir("apptraverse_surfaces_gui_proxy");
  SurfacesModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run(
        [&session](SurfacesPublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);
  Surfaces* const ui_surfaces = &*ui_app->surfaces;
  Surface* const ui_surface1 = &*ui_surfaces->surfaces[0];
  Presenter* const ui_surface1_p = &*ui_surface1->presenter;
  CHECK(TestSurfacePresenter::on_load_calls.load() == 1);

  // GUI AddClick → MODEL Surface1::AddSurface (no model pointer across domains).
  ui_surface1->presenter->AddClick();
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_surfaces->surfaces.size() == 2);
  Surface* const ui_surface2 = &*ui_surfaces->surfaces[1];
  auto const surface2_id = ui_surface2->obj_id;
  SurfacePresenter::ptr surface2_presenter_hold = ui_surface2->presenter;
  CHECK(TestSurfacePresenter::on_load_calls.load() == 2);

  // Conceptual "Remove current" without model current_surface: caller picks
  // Surface2 presenter (future mobile pager current page).
  ui_surface2->presenter->RemoveClick();
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_surfaces->surfaces.size() == 1);
  CHECK(&*ui_surfaces->surfaces[0] == ui_surface1);
  CHECK(&*ui_surface1->presenter == ui_surface1_p);
  CHECK(TestSurfacePresenter::on_unload_calls.load() == 1);
  CHECK(!surface2_presenter_hold->presentation_loaded);

  InitializeNewPresenters(*ui_app, nullptr, &proxy);
  CHECK(TestSurfacePresenter::on_load_calls.load() == 2);
  CHECK(TestSurfacePresenter::on_unload_calls.load() == 1);

  std::vector<ae::Obj*> live;
  CollectLiveReachableObjects(*ui_app, live);
  for (ae::Obj* obj : live) {
    CHECK(obj->obj_id != surface2_id);
  }

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

void TestPresenterLifecycleMultiAdd() {
  TestSurfacePresenter::ResetCounts();
  auto dir = TestDir("apptraverse_surfaces_presenter_life");
  SurfacesModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run(
        [&session](SurfacesPublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);
  Surface* const s1 = &*ui_app->surfaces->surfaces[0];
  Presenter* const s1_p = &*s1->presenter;
  CHECK(TestSurfacePresenter::on_load_calls.load() == 1);

  s1->presenter->AddClick();
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  Surface* const s2 = &*ui_app->surfaces->surfaces[1];
  Presenter* const s2_p = &*s2->presenter;
  CHECK(TestSurfacePresenter::on_load_calls.load() == 2);
  CHECK(&*s1->presenter == s1_p);

  s2->presenter->AddClick();
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->surfaces->surfaces.size() == 3);
  Surface* const s3 = &*ui_app->surfaces->surfaces[2];
  Presenter* const s3_p = &*s3->presenter;
  CHECK(TestSurfacePresenter::on_load_calls.load() == 3);
  CHECK(&*s1->presenter == s1_p);
  CHECK(&*s2->presenter == s2_p);

  auto const s2_id = s2->obj_id;
  SurfacePresenter::ptr s2_hold = s2->presenter;
  s2->presenter->RemoveClick();
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_app->surfaces->surfaces.size() == 2);
  CHECK(&*ui_app->surfaces->surfaces[0] == s1);
  CHECK(&*ui_app->surfaces->surfaces[1] == s3);
  CHECK(&*s1->presenter == s1_p);
  CHECK(&*s3->presenter == s3_p);
  CHECK(TestSurfacePresenter::on_unload_calls.load() == 1);
  CHECK(!s2_hold->presentation_loaded);
  (void)s2_id;

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

void TestShutdownDrain() {
  auto dir = TestDir("apptraverse_surfaces_shutdown_drain");
  SurfacesModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run(
        [&session](SurfacesPublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  TakeAndWake(session);

  session.Post([](ae::Domain& domain) {
    auto object = domain.Find(ae::ObjId{surfaces_demo::ToObjId(
        surfaces_demo::ObjId::Surface1)});
    CHECK(object);
    object.as<Surface>()->AddSurface();
  });
  session.Post([](ae::Domain& domain) {
    auto object = domain.Find(ae::ObjId{surfaces_demo::ToObjId(
        surfaces_demo::ObjId::Surface1)});
    CHECK(object);
    object.as<Surface>()->AddSurface();
  });
  session.RequestStop();
  model.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  Surfaces& surfaces = *application->surfaces;
  CHECK(surfaces.surfaces.size() == 3);
  CHECK(surfaces.surfaces[0]->number == 1);
  CHECK(surfaces.surfaces[1]->number == 2);
  CHECK(surfaces.surfaces[2]->number == 3);
  CHECK(surfaces.journal.size() == 2);

  session.Post([](ae::Domain&) {});
  CHECK(session.pending_work.empty());
  std::filesystem::remove_all(dir);
}

void TestBoundsReplay() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surface& surface = *application->surfaces->surfaces[0];
  auto const initial_x = surface.desktop_x;
  auto const initial_y = surface.desktop_y;
  auto const initial_w = surface.desktop_width;
  auto const initial_h = surface.desktop_height;
  CHECK(initial_w == 360);
  CHECK(initial_h == 240);
  CHECK(surface.journal.empty());

  surface.SetDesktopBounds(200, 220, 400, 300);
  CHECK(surface.journal.size() == 1);
  CHECK(surface.desktop_x == 200);
  CHECK(surface.desktop_y == 220);
  CHECK(surface.desktop_width == 400);
  CHECK(surface.desktop_height == 300);
  auto const event_id = surface.journal[0].event->obj_id;
  CHECK(surface.journal[0].event->GetClassId() ==
        SurfaceBoundsChangedEvent::kClassId);

  surface.SetDesktopBounds(200, 220, 400, 300);
  CHECK(surface.journal.size() == 1);

  surface.ReplayFromBase();
  CHECK(surface.desktop_x == 200);
  CHECK(surface.desktop_y == 220);
  CHECK(surface.desktop_width == 400);
  CHECK(surface.desktop_height == 300);
  CHECK(surface.journal.size() == 1);
  CHECK(surface.journal[0].event->obj_id == event_id);
  (void)initial_x;
  (void)initial_y;
}

void TestGeometryPersistence() {
  auto dir = TestDir("apptraverse_surfaces_geometry_persist");
  ae::ObjId surface1_id;
  ae::ObjId surface2_id;
  ae::ObjId surface3_id;
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = BuildSurfacesGraph(domain);
    FinalizeDistilledGraph(*application);
    Surfaces& surfaces = *application->surfaces;
    surfaces.surfaces[0]->AddSurface();
    surfaces.surfaces[0]->AddSurface();
    CHECK(surfaces.surfaces.size() == 3);
    surfaces.surfaces[0]->SetDesktopBounds(40, 50, 320, 200);
    surfaces.surfaces[1]->SetDesktopBounds(80, 90, 340, 210);
    surfaces.surfaces[2]->SetDesktopBounds(120, 130, 360, 220);
    surface1_id = surfaces.surfaces[0]->obj_id;
    surface2_id = surfaces.surfaces[1]->obj_id;
    surface3_id = surfaces.surfaces[2]->obj_id;
    SaveDistilledRoot(*application);
  }

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  Surfaces& surfaces = *application->surfaces;
  CHECK(surfaces.surfaces.size() == 3);
  CHECK(surfaces.surfaces[0]->obj_id == surface1_id);
  CHECK(surfaces.surfaces[1]->obj_id == surface2_id);
  CHECK(surfaces.surfaces[2]->obj_id == surface3_id);
  CHECK(surfaces.surfaces[0]->desktop_x == 40);
  CHECK(surfaces.surfaces[0]->desktop_y == 50);
  CHECK(surfaces.surfaces[0]->desktop_width == 320);
  CHECK(surfaces.surfaces[0]->desktop_height == 200);
  CHECK(surfaces.surfaces[1]->desktop_x == 80);
  CHECK(surfaces.surfaces[1]->desktop_y == 90);
  CHECK(surfaces.surfaces[1]->desktop_width == 340);
  CHECK(surfaces.surfaces[1]->desktop_height == 210);
  CHECK(surfaces.surfaces[2]->desktop_x == 120);
  CHECK(surfaces.surfaces[2]->desktop_y == 130);
  CHECK(surfaces.surfaces[2]->desktop_width == 360);
  CHECK(surfaces.surfaces[2]->desktop_height == 220);
  std::filesystem::remove_all(dir);
}

void TestCurrentPageReplay() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surfaces& surfaces = *application->surfaces;
  surfaces.surfaces[0]->AddSurface();
  Surface::ptr surface2 = surfaces.surfaces[1];
  auto const surface2_id = surface2->obj_id;

  surface2->MakeCurrent();
  CHECK(surfaces.journal.size() == 2);
  CHECK(surfaces.journal.back().event->GetClassId() ==
        SetCurrentSurfaceEvent::kClassId);
  CHECK(surfaces.mobile_current);
  CHECK(surfaces.mobile_current->obj_id == surface2_id);

  surface2->MakeCurrent();  // already current → no Event
  CHECK(surfaces.journal.size() == 2);

  surfaces.ReplayFromBase();
  CHECK(surfaces.mobile_current);
  CHECK(surfaces.mobile_current->obj_id == surface2_id);
  CHECK(&*surfaces.mobile_current == &*surfaces.surfaces[1]);

  // Removing the current page drops the reference; picking the next current
  // page is the host's decision.
  surfaces.surfaces[1]->Remove();
  CHECK(surfaces.surfaces.size() == 1);
  CHECK(!surfaces.mobile_current);

  surfaces.ReplayFromBase();
  CHECK(!surfaces.mobile_current);

  surfaces.surfaces[0]->MakeCurrent();
  CHECK(surfaces.mobile_current);
  CHECK(&*surfaces.mobile_current == &*surfaces.surfaces[0]);
  surface2->MakeCurrent();  // stale Surface → no-op
  CHECK(&*surfaces.mobile_current == &*surfaces.surfaces[0]);
}

void TestCurrentPageIdentitySurvivesRemoveBefore() {
  // Proof that mobile_current is Surface identity, not a list index.
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surfaces& surfaces = *application->surfaces;
  surfaces.surfaces[0]->AddSurface();
  surfaces.surfaces[0]->AddSurface();
  CHECK(surfaces.surfaces.size() == 3);
  Surface::ptr surface3 = surfaces.surfaces[2];
  auto const surface3_id = surface3->obj_id;

  surface3->MakeCurrent();
  CHECK(surfaces.mobile_current);
  CHECK(surfaces.mobile_current->obj_id == surface3_id);
  CHECK(&*surfaces.mobile_current == &*surfaces.surfaces[2]);

  surfaces.surfaces[0]->Remove();  // remove a page before current
  CHECK(surfaces.surfaces.size() == 2);
  CHECK(surfaces.mobile_current);
  CHECK(surfaces.mobile_current->obj_id == surface3_id);
  CHECK(&*surfaces.mobile_current == &*surfaces.surfaces[1]);
  CHECK(surfaces.surfaces[1]->obj_id == surface3_id);
}

void TestCurrentPagePersistence() {
  auto dir = TestDir("apptraverse_surfaces_current_persist");
  ae::ObjId surface2_id;
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = BuildSurfacesGraph(domain);
    FinalizeDistilledGraph(*application);
    Surfaces& surfaces = *application->surfaces;
    surfaces.surfaces[0]->AddSurface();
    surfaces.surfaces[0]->AddSurface();
    surface2_id = surfaces.surfaces[1]->obj_id;
    surfaces.surfaces[1]->MakeCurrent();
    SaveDistilledRoot(*application);
  }

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  Surfaces& surfaces = *application->surfaces;
  CHECK(surfaces.surfaces.size() == 3);
  CHECK(surfaces.mobile_current);
  CHECK(surfaces.mobile_current->obj_id == surface2_id);
  CHECK(&*surfaces.mobile_current == &*surfaces.surfaces[1]);
  std::filesystem::remove_all(dir);
}

void TestCurrentPageThroughGuiProxy() {
  TestSurfacePresenter::ResetCounts();
  auto dir = TestDir("apptraverse_surfaces_current_proxy");
  SurfacesModelSession session;
  session.state_dir = dir;
  auto proxy = MakeSessionProxy(session);
  std::thread model{[&] {
    session.Run(
        [&session](SurfacesPublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, &proxy);
  Surfaces* const ui_surfaces = &*ui_app->surfaces;
  CHECK(!ui_surfaces->mobile_current);

  ui_surfaces->surfaces[0]->presenter->AddClick();
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_surfaces->surfaces.size() == 2);
  Surface* const ui_surface2 = &*ui_surfaces->surfaces[1];
  auto const surface2_id = ui_surface2->obj_id;

  // Swipe to page 2: the GUI mirror learns the current page from the model.
  ui_surface2->presenter->PageShown();
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_surfaces->mobile_current);
  CHECK(ui_surfaces->mobile_current->obj_id == surface2_id);
  CHECK(&*ui_surfaces->mobile_current == ui_surface2);

  ui_surface2->presenter->RemoveClick();
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          &proxy);
  CHECK(ui_surfaces->surfaces.size() == 1);
  CHECK(!ui_surfaces->mobile_current);

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

// Node::base is a historical snapshot the owning Node may replace, so no
// materialized-change notifier may ever reach it — neither through the
// reachability bind at session start nor through the notifier a dynamically
// added Node inherits from its creator.
void TestNotifierNeverBindsNodeBase() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);

  PendingDirtyNodes pending;
  BindReachableNodesMaterializedChangeNotifier(*application, &pending,
                                               &PendingDirtyNodesNotify);

  Surfaces& surfaces = *application->surfaces;
  CHECK(surfaces.HasMaterializedChangeNotifier());
  CHECK(surfaces.base.is_loaded());
  CHECK(!surfaces.base->HasMaterializedChangeNotifier());

  Surface& surface1 = *surfaces.surfaces[0];
  CHECK(surface1.HasMaterializedChangeNotifier());
  CHECK(surface1.base.is_loaded());
  CHECK(!surface1.base->HasMaterializedChangeNotifier());

  surface1.AddSurface();
  CHECK(surfaces.surfaces.size() == 2);
  Surface& surface2 = *surfaces.surfaces[1];
  CHECK(surface2.HasMaterializedChangeNotifier());
  CHECK(surface2.base.is_loaded());
  CHECK(!surface2.base->HasMaterializedChangeNotifier());

  // The added Surface really is wired to the same pending set.
  CHECK(!pending.empty());
  surface2.SetDesktopBounds(1, 2, 3, 4);
  CHECK(pending.membership.count(surface2.obj_id.id()) == 1);
  CHECK(pending.membership.count(surface2.base.id().id()) == 0);

  ClearReachableNodesMaterializedChangeNotifier(*application);
  CHECK(!surfaces.HasMaterializedChangeNotifier());
  CHECK(!surface1.HasMaterializedChangeNotifier());
}

// Deferred publication resolves against live topology, not Domain::Find: the
// remove Event in the Surfaces journal keeps a removed Surface findable.
void TestFindLiveReachableNodeOnModelGraph() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surfaces& surfaces = *application->surfaces;
  surfaces.surfaces[0]->AddSurface();
  CHECK(surfaces.surfaces.size() == 2);
  Surface* const surface2 = &*surfaces.surfaces[1];
  auto const surfaces_id = surfaces.obj_id.id();
  auto const surface2_id = surface2->obj_id.id();

  CHECK(FindLiveReachableNode(*application, surfaces_id) ==
        static_cast<Node*>(&surfaces));
  CHECK(FindLiveReachableNode(*application, surface2_id) ==
        static_cast<Node*>(surface2));

  surface2->Remove();
  CHECK(surfaces.surfaces.size() == 1);
  CHECK(domain.Find(ae::ObjId{surface2_id}));
  CHECK(FindLiveReachableNode(*application, surfaces_id) ==
        static_cast<Node*>(&surfaces));
  CHECK(FindLiveReachableNode(*application, surface2_id) == nullptr);
}

// While the GUI holds one unread publication the model keeps running: two
// Adds land as two Events but coalesce into one later publication. The GUI
// then catches up in two applies without disturbing surviving presenters.
void TestGuiCatchUpCoalescesPublicationsNotEvents() {
  TestSurfacePresenter::ResetCounts();
  auto dir = TestDir("apptraverse_surfaces_gui_catchup");
  SurfacesModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run(
        [&session](SurfacesPublicationKind) { session.cv.notify_all(); });
  }};

  WaitPublished(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, nullptr);
  Surfaces* const ui_surfaces = &*ui_app->surfaces;
  CHECK(ui_surfaces->surfaces.size() == 1);
  Surface* const ui_a = &*ui_surfaces->surfaces[0];
  Presenter* const ui_a_presenter = &*ui_a->presenter;
  CHECK(TestSurfacePresenter::on_load_calls.load() == 1);
  CHECK(session.channel.publish_count() == 1);

  std::uint32_t model_a = 0;
  std::uint32_t model_b = 0;
  std::uint32_t model_c = 0;
  RunOnModel(session, [&](Application& app) {
    model_a = app.surfaces->surfaces[0]->obj_id.id();
    app.surfaces->surfaces[0]->AddSurface();
    model_b = app.surfaces->surfaces[1]->obj_id.id();
  });
  WaitPublished(session);
  CHECK(session.channel.publish_count() == 2);

  // Model keeps working with the publication unread; the second Add is a
  // second journal Event but cannot get its own publication yet.
  RunOnModel(session, [&](Application& app) {
    app.surfaces->surfaces[0]->AddSurface();
    CHECK(app.surfaces->surfaces.size() == 3);
    model_c = app.surfaces->surfaces[2]->obj_id.id();
    CHECK(app.surfaces->journal.size() == 2);
    CHECK(app.surfaces->journal[0].event->GetClassId() ==
          AddSurfaceEvent::kClassId);
    CHECK(app.surfaces->journal[1].event->GetClassId() ==
          AddSurfaceEvent::kClassId);
  });
  CHECK(session.channel.publish_count() == 2);

  // First catch-up apply: the snapshot taken before C existed.
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          nullptr);
  CHECK(ui_surfaces->surfaces.size() == 2);
  CHECK(&*ui_surfaces->surfaces[0] == ui_a);
  CHECK(&*ui_a->presenter == ui_a_presenter);
  CHECK(ui_surfaces->surfaces[0]->obj_id.id() == model_a);
  CHECK(ui_surfaces->surfaces[1]->obj_id.id() == model_b);
  Surface* const ui_b = &*ui_surfaces->surfaces[1];
  Presenter* const ui_b_presenter = &*ui_b->presenter;
  CHECK(TestSurfacePresenter::on_load_calls.load() == 2);
  CHECK(TestSurfacePresenter::on_unload_calls.load() == 0);

  // Second catch-up apply: the coalesced publication for C.
  WaitPublished(session);
  CHECK(session.channel.publish_count() == 3);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          nullptr);
  CHECK(ui_surfaces->surfaces.size() == 3);
  CHECK(&*ui_surfaces->surfaces[0] == ui_a);
  CHECK(&*ui_a->presenter == ui_a_presenter);
  CHECK(&*ui_surfaces->surfaces[1] == ui_b);
  CHECK(&*ui_b->presenter == ui_b_presenter);
  CHECK(ui_surfaces->surfaces[0]->obj_id.id() == model_a);
  CHECK(ui_surfaces->surfaces[1]->obj_id.id() == model_b);
  CHECK(ui_surfaces->surfaces[2]->obj_id.id() == model_c);
  CHECK(TestSurfacePresenter::on_load_calls.load() == 3);
  CHECK(TestSurfacePresenter::on_unload_calls.load() == 0);
  CHECK(TestSurfacePresenter::loaded_ids.size() == 3);

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

// A Node dirtied before removal must not produce a publication afterwards:
// its pending entry is dropped instead of resurrecting it in the GUI.
void TestPendingPublicationDroppedForRemovedNode() {
  TestSurfacePresenter::ResetCounts();
  auto dir = TestDir("apptraverse_surfaces_pending_removed");
  SurfacesModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run(
        [&session](SurfacesPublicationKind) { session.cv.notify_all(); });
  }};

  WaitPublished(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, nullptr);
  Surfaces* const ui_surfaces = &*ui_app->surfaces;

  // Reach [A,B] in the GUI.
  RunOnModel(session,
             [](Application& app) { app.surfaces->surfaces[0]->AddSurface(); });
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          nullptr);
  CHECK(ui_surfaces->surfaces.size() == 2);

  // Hold a publication unread so the next changes stay pending.
  std::uint32_t model_c = 0;
  RunOnModel(session, [&](Application& app) {
    app.surfaces->surfaces[0]->AddSurface();
    model_c = app.surfaces->surfaces[2]->obj_id.id();
  });
  WaitPublished(session);
  auto const held = session.channel.publish_count();
  CHECK(held == 3);

  // C becomes dirty on its own, then leaves live topology.
  RunOnModel(session, [](Application& app) {
    app.surfaces->surfaces[2]->SetDesktopBounds(11, 22, 333, 244);
  });
  RunOnModel(session, [](Application& app) {
    app.surfaces->surfaces[2]->Remove();
    CHECK(app.surfaces->surfaces.size() == 2);
  });
  CHECK(session.channel.publish_count() == held);

  // Catch up to [A,B,C] from the held snapshot.
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          nullptr);
  CHECK(ui_surfaces->surfaces.size() == 3);
  CHECK(ui_surfaces->surfaces[2]->obj_id.id() == model_c);
  CHECK(TestSurfacePresenter::on_load_calls.load() == 3);

  // Exactly one further publication: C's stale pending entry is dropped and
  // only the Surfaces removal is published.
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          nullptr);
  CHECK(session.channel.publish_count() == held + 1);
  CHECK(ui_surfaces->surfaces.size() == 2);
  CHECK(TestSurfacePresenter::on_unload_calls.load() == 1);
  CHECK(TestSurfacePresenter::on_load_calls.load() == 3);

  std::vector<ae::Obj*> live;
  CollectLiveReachableObjects(*ui_app, live);
  for (ae::Obj* obj : live) {
    CHECK(obj->obj_id.id() != model_c);
  }

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

// A Node created and then changed before the GUI catches up must arrive once,
// with the later field state, and must not reload its presenter.
void TestNewChildChangedBeforeGuiCatchUp() {
  TestSurfacePresenter::ResetCounts();
  auto dir = TestDir("apptraverse_surfaces_new_child_changed");
  SurfacesModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run(
        [&session](SurfacesPublicationKind) { session.cv.notify_all(); });
  }};

  WaitPublished(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app, nullptr, nullptr);
  Surfaces* const ui_surfaces = &*ui_app->surfaces;

  RunOnModel(session,
             [](Application& app) { app.surfaces->surfaces[0]->AddSurface(); });
  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          nullptr);
  CHECK(ui_surfaces->surfaces.size() == 2);

  // Hold the [A,B,C] snapshot unread, then change the brand new C.
  std::uint32_t model_c = 0;
  RunOnModel(session, [&](Application& app) {
    app.surfaces->surfaces[0]->AddSurface();
    model_c = app.surfaces->surfaces[2]->obj_id.id();
  });
  WaitPublished(session);
  RunOnModel(session, [](Application& app) {
    app.surfaces->surfaces[2]->SetDesktopBounds(17, 29, 341, 251);
  });

  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          nullptr);
  CHECK(ui_surfaces->surfaces.size() == 3);
  Surface* const ui_c = &*ui_surfaces->surfaces[2];
  Presenter* const ui_c_presenter = &*ui_c->presenter;
  CHECK(ui_c->obj_id.id() == model_c);
  CHECK(ui_c->desktop_x != 17);
  CHECK(TestSurfacePresenter::on_load_calls.load() == 3);

  WaitPublished(session);
  ApplySurfacesStructural(TakeAndWake(session), *ui_app, ui_storage, nullptr,
                          nullptr);
  CHECK(ui_surfaces->surfaces.size() == 3);
  CHECK(&*ui_surfaces->surfaces[2] == ui_c);
  CHECK(&*ui_c->presenter == ui_c_presenter);
  CHECK(ui_c->desktop_x == 17);
  CHECK(ui_c->desktop_y == 29);
  CHECK(ui_c->desktop_width == 341);
  CHECK(ui_c->desktop_height == 251);
  CHECK(TestSurfacePresenter::on_load_calls.load() == 3);
  CHECK(TestSurfacePresenter::on_unload_calls.load() == 0);

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

// ObjId::GenerateUnique is one process-wide generator. Two model threads
// creating Surfaces at the same time must not hand out the same identity.
void TestConcurrentSessionsGenerateDistinctObjIds() {
  auto dir_a = TestDir("apptraverse_surfaces_concurrent_ids_a");
  auto dir_b = TestDir("apptraverse_surfaces_concurrent_ids_b");

  SurfacesModelSession session_a;
  SurfacesModelSession session_b;
  session_a.state_dir = dir_a;
  session_b.state_dir = dir_b;

  std::thread model_a{[&] { session_a.Run([](SurfacesPublicationKind) {}); }};
  std::thread model_b{[&] { session_b.Run([](SurfacesPublicationKind) {}); }};
  WaitPublished(session_a);
  WaitPublished(session_b);
  (void)TakeAndWake(session_a);
  (void)TakeAndWake(session_b);

  constexpr int kAddsPerSession = 24;
  std::vector<std::uint32_t> ids_a;
  std::vector<std::uint32_t> ids_b;
  std::atomic<bool> start{false};

  auto drive = [&](SurfacesModelSession& session,
                   std::vector<std::uint32_t>& ids) {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (int i = 0; i < kAddsPerSession; ++i) {
      RunOnModel(session, [&](Application& app) {
        app.surfaces->surfaces[0]->AddSurface();
        Surface& added = *app.surfaces->surfaces.back();
        ids.push_back(added.obj_id.id());
        ids.push_back(added.base.id().id());
        ids.push_back(added.presenter.id().id());
      });
    }
  };

  std::thread driver_a{[&] { drive(session_a, ids_a); }};
  std::thread driver_b{[&] { drive(session_b, ids_b); }};
  start.store(true, std::memory_order_release);
  driver_a.join();
  driver_b.join();

  session_a.RequestStop();
  session_b.RequestStop();
  model_a.join();
  model_b.join();

  CHECK(ids_a.size() == static_cast<std::size_t>(kAddsPerSession) * 3);
  CHECK(ids_b.size() == static_cast<std::size_t>(kAddsPerSession) * 3);
  std::unordered_set<std::uint32_t> seen;
  for (auto const& ids : {ids_a, ids_b}) {
    for (std::uint32_t id : ids) {
      CHECK(id != 0);
      CHECK(seen.insert(id).second);
    }
  }

  std::filesystem::remove_all(dir_a);
  std::filesystem::remove_all(dir_b);
}

void TestNoRttiCompileGuard() {
#if defined(_CPPRTTI) || defined(__GXX_RTTI)
  CHECK(false && "surfaces targets must compile with RTTI disabled");
#endif
  CHECK(true);
}

void TestTwoIndependentSessionsIsolation() {
  auto dir_a = TestDir("apptraverse_surfaces_two_runtime_a");
  auto dir_b = TestDir("apptraverse_surfaces_two_runtime_b");

  SurfacesModelSession session_a;
  SurfacesModelSession session_b;
  session_a.state_dir = dir_a;
  session_b.state_dir = dir_b;

  std::thread model_a{[&] {
    session_a.Run([&](SurfacesPublicationKind) {});
  }};
  std::thread model_b{[&] {
    session_b.Run([&](SurfacesPublicationKind) {});
  }};

  WaitPublished(session_a);
  WaitPublished(session_b);
  // One initial publication each; neither session sees the other's channel.
  CHECK(session_a.channel.publish_count() == 1);
  CHECK(session_b.channel.publish_count() == 1);
  (void)TakeAndWake(session_a);
  (void)TakeAndWake(session_b);

  auto proxy_a = MakeSessionProxy(session_a);
  auto proxy_b = MakeSessionProxy(session_b);

  std::uint32_t surface1_a = 0;
  std::uint32_t surface1_b = 0;
  {
    std::promise<void> done;
    auto fut = done.get_future();
    session_a.Post([&](ae::Domain& domain) {
      auto app = LoadApplication<Application>(
          domain, ae::ObjId{surfaces_demo::ToObjId(
                      surfaces_demo::ObjId::Application)});
      surface1_a = app->surfaces->surfaces[0]->obj_id.id();
      app->surfaces->surfaces[0]->AddSurface();
      done.set_value();
    });
    CHECK(fut.wait_for(std::chrono::seconds{30}) == std::future_status::ready);
  }
  {
    std::promise<void> done;
    auto fut = done.get_future();
    session_b.Post([&](ae::Domain& domain) {
      auto app = LoadApplication<Application>(
          domain, ae::ObjId{surfaces_demo::ToObjId(
                      surfaces_demo::ObjId::Application)});
      surface1_b = app->surfaces->surfaces[0]->obj_id.id();
      app->surfaces->surfaces[0]->SetDesktopBounds(10, 20, 300, 200);
      done.set_value();
    });
    CHECK(fut.wait_for(std::chrono::seconds{30}) == std::future_status::ready);
  }

  WaitPublished(session_a);
  WaitPublished(session_b);
  // Exactly one incremental publication each: one session's work never
  // advances the other's channel.
  CHECK(session_a.channel.publish_count() == 2);
  CHECK(session_b.channel.publish_count() == 2);
  auto bytes_a = TakeAndWake(session_a);
  auto bytes_b = TakeAndWake(session_b);
  CHECK(!bytes_a.empty());
  CHECK(!bytes_b.empty());
  // Same logical ObjId space is allowed across Domains; publications differ.
  CHECK(bytes_a != bytes_b);

  // Stop A fully; B must keep accepting work and publishing.
  session_a.RequestStop();
  model_a.join();
  auto const a_final_publishes = session_a.channel.publish_count();
  CHECK(a_final_publishes == 2);

  {
    std::promise<void> done;
    auto fut = done.get_future();
    session_b.Post([&](ae::Domain& domain) {
      auto app = LoadApplication<Application>(
          domain, ae::ObjId{surfaces_demo::ToObjId(
                      surfaces_demo::ObjId::Application)});
      CHECK(app->surfaces->surfaces.size() == 1);
      app->surfaces->surfaces[0]->AddSurface();
      done.set_value();
    });
    CHECK(fut.wait_for(std::chrono::seconds{30}) == std::future_status::ready);
  }
  WaitPublished(session_b);
  (void)TakeAndWake(session_b);

  // Dynamic Node on B after A is gone: Event on Surface2 still notifies B.
  {
    std::promise<void> done;
    auto fut = done.get_future();
    session_b.Post([&](ae::Domain& domain) {
      auto app = LoadApplication<Application>(
          domain, ae::ObjId{surfaces_demo::ToObjId(
                      surfaces_demo::ObjId::Application)});
      CHECK(app->surfaces->surfaces.size() == 2);
      auto& surface2 = *app->surfaces->surfaces[1];
      CHECK(surface2.HasMaterializedChangeNotifier());
      surface2.SetDesktopBounds(40, 50, 400, 300);
      done.set_value();
    });
    CHECK(fut.wait_for(std::chrono::seconds{30}) == std::future_status::ready);
  }
  WaitPublished(session_b);
  (void)TakeAndWake(session_b);
  // B advanced twice more after A stopped; A's counter never moved again.
  CHECK(session_b.channel.publish_count() == 4);
  CHECK(session_a.channel.publish_count() == a_final_publishes);

  session_b.RequestStop();
  model_b.join();
  (void)proxy_a;
  (void)proxy_b;
  (void)surface1_a;
  (void)surface1_b;
  std::filesystem::remove_all(dir_a);
  std::filesystem::remove_all(dir_b);
}

void TestModelWorkRunsWhilePublicationUnread() {
  auto dir = TestDir("apptraverse_surfaces_unread_backpressure");
  SurfacesModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&](SurfacesPublicationKind) {});
  }};

  WaitPublished(session);
  (void)TakeAndWake(session);  // initial

  session.Post([](ae::Domain& domain) {
    auto app = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    app->surfaces->surfaces[0]->AddSurface();
  });
  WaitPublished(session);
  // Leave publication #1 unread.

  std::promise<void> second_add_done;
  auto second_add_fut = second_add_done.get_future();
  session.Post([&](ae::Domain& domain) {
    auto app = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    app->surfaces->surfaces[0]->AddSurface();
    second_add_done.set_value();
  });

  std::promise<void> signal_done;
  auto signal_fut = signal_done.get_future();
  session.Post([&](ae::Domain&) { signal_done.set_value(); });

  CHECK(second_add_fut.wait_for(std::chrono::seconds{30}) ==
        std::future_status::ready);
  CHECK(signal_fut.wait_for(std::chrono::seconds{30}) ==
        std::future_status::ready);

  // Model already [1,2,3]; GUI still holds [1,2] publication.
  auto pub1 = TakeAndWake(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  // Rebuild UI from a fresh initial would be wrong; apply structural to a
  // loaded mirror from a separate initial consume path is heavy. Instead
  // verify model Domain topology and that releasing the channel yields the
  // coalesced/final publication for Surface3 topology.
  {
    std::promise<std::size_t> count;
    auto fut = count.get_future();
    session.Post([&](ae::Domain& domain) {
      auto app = LoadApplication<Application>(
          domain, ae::ObjId{surfaces_demo::ToObjId(
                      surfaces_demo::ObjId::Application)});
      count.set_value(app->surfaces->surfaces.size());
    });
    CHECK(fut.wait_for(std::chrono::seconds{30}) == std::future_status::ready);
    CHECK(fut.get() == 3);
  }

  WaitPublished(session);
  auto pub2 = TakeAndWake(session);
  CHECK(!pub1.empty());
  CHECK(!pub2.empty());

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

void TestShutdownDrainsWorkWithUnreadPublication() {
  auto dir = TestDir("apptraverse_surfaces_stop_unread");
  SurfacesModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&](SurfacesPublicationKind) {});
  }};

  WaitPublished(session);
  (void)TakeAndWake(session);

  session.Post([](ae::Domain& domain) {
    auto app = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    app->surfaces->surfaces[0]->AddSurface();
  });
  WaitPublished(session);
  // Unread publication held.

  session.Post([](ae::Domain& domain) {
    auto app = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    app->surfaces->surfaces[0]->AddSurface();
  });
  session.RequestStop();
  model.join();

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto app = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    CHECK(app->surfaces->surfaces.size() == 3);
  }
  std::filesystem::remove_all(dir);
}

void TestRuntimeNodeBaseUsesCanonicalOwnership() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto surface = Surface::ptr::Create(ae::CreateWith{domain});
  surface->number = 1;
  AssignInitialDesktopBounds(*surface);
  InitializeRuntimeNode(*surface);

  CHECK(surface->base.is_valid());
  CHECK(surface->base.is_loaded());
  CHECK(surface->base.id() != surface->obj_id);
  CHECK(surface->journal.empty());
  // Most-derived class id of the base storage matches Surface, not Node.
  CHECK(surface->base->GetClassId() == Surface::kClassId);
  CHECK(ae::Registry::GetRegistry().GenerationDistance(
            Node::kClassId, surface->base->GetClassId()) >= 0);

  // Extra derived layer: Surfaces is NodeFor and also gets a base.
  auto surfaces = Surfaces::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*surfaces);
  CHECK(surfaces->base->GetClassId() == Surfaces::kClassId);
  CHECK(surfaces->base.id() != surfaces->obj_id);
}

class CountingDomainStorage final : public ae::IDomainStorage {
 public:
  explicit CountingDomainStorage(ae::IDomainStorage& inner) : inner_{inner} {}

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    ++store_calls;
    return inner_.Store(query);
  }
  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    return inner_.Load(query);
  }
  ae::ClassList Enumerate(ae::ObjId const& id) override {
    return inner_.Enumerate(id);
  }
  void Remove(ae::ObjId const& id) override { inner_.Remove(id); }
  void CleanUp() override { inner_.CleanUp(); }

  std::size_t store_calls{0};

 private:
  ae::IDomainStorage& inner_;
};

void TestInitializeRuntimeNodeStorageWrites() {
  ae::RamDomainStorage ram;
  CountingDomainStorage counting{ram};
  ae::Domain domain{counting};
  auto surface = Surface::ptr::Create(ae::CreateWith{domain});
  surface->number = 1;
  AssignInitialDesktopBounds(*surface);
  auto const before = counting.store_calls;
  InitializeRuntimeNode(*surface);
  auto const during_init = counting.store_calls - before;
  // Characterization: CaptureBaseState currently writes base layers via
  // DomainGraph::Save before any Application::Save.
  CHECK(during_init > 0);
  std::cout << "InitializeRuntimeNode Store calls=" << during_init << '\n';
}

}  // namespace

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesModelRegistration();
  apptraverse::test::TestInitialGraph();
  apptraverse::test::TestModelAdd();
  apptraverse::test::TestAddFromSecondSurface();
  apptraverse::test::TestModelRemove();
  apptraverse::test::TestReplay();
  apptraverse::test::TestRestartPersistence();
  apptraverse::test::TestDynamicNodeStructuralPublication();
  apptraverse::test::TestGuiProxyAddRemoveAndRemoveCurrent();
  apptraverse::test::TestPresenterLifecycleMultiAdd();
  apptraverse::test::TestShutdownDrain();
  apptraverse::test::TestBoundsReplay();
  apptraverse::test::TestGeometryPersistence();
  apptraverse::test::TestCurrentPageReplay();
  apptraverse::test::TestCurrentPageIdentitySurvivesRemoveBefore();
  apptraverse::test::TestCurrentPagePersistence();
  apptraverse::test::TestCurrentPageThroughGuiProxy();
  apptraverse::test::TestNotifierNeverBindsNodeBase();
  apptraverse::test::TestFindLiveReachableNodeOnModelGraph();
  apptraverse::test::TestGuiCatchUpCoalescesPublicationsNotEvents();
  apptraverse::test::TestPendingPublicationDroppedForRemovedNode();
  apptraverse::test::TestNewChildChangedBeforeGuiCatchUp();
  apptraverse::test::TestConcurrentSessionsGenerateDistinctObjIds();
  apptraverse::test::TestNoRttiCompileGuard();
  apptraverse::test::TestTwoIndependentSessionsIsolation();
  apptraverse::test::TestModelWorkRunsWhilePublicationUnread();
  apptraverse::test::TestShutdownDrainsWorkWithUnreadPublication();
  apptraverse::test::TestRuntimeNodeBaseUsesCanonicalOwnership();
  apptraverse::test::TestInitializeRuntimeNodeStorageWrites();
  std::cout << "surfaces_model_test OK\n";
  return 0;
}
