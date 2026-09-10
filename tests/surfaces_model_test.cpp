#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
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

void TestNoRttiCompileGuard() {
#if defined(_CPPRTTI) || defined(__GXX_RTTI)
  CHECK(false && "surfaces targets must compile with RTTI disabled");
#endif
  CHECK(true);
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
  apptraverse::test::TestNoRttiCompileGuard();
  std::cout << "surfaces_model_test OK\n";
  return 0;
}
