#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "surfaces_distill.h"
#include "surfaces_lifecycle.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

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

void TakeAndWake(SurfacesModelSession& session) {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session.mu};
    bytes = session.channel.TakePublishedCopy();
  }
  session.cv.notify_all();
  CHECK(!bytes.empty());
}

void RunOnModel(SurfacesModelSession& session,
                std::function<void(Application&)> body) {
  std::promise<void> done;
  auto future = done.get_future();
  session.Post([&](ae::Domain& domain) {
    auto app = domain.Find(ae::ObjId{surfaces_demo::ToObjId(
        surfaces_demo::ObjId::Application)});
    CHECK(app);
    body(static_cast<Application&>(*app));
    done.set_value();
  });
  CHECK(future.wait_for(std::chrono::seconds{120}) ==
        std::future_status::ready);
}

void SeedLongJournal(SurfacesModelSession& session, int event_count) {
  RunOnModel(session, [event_count](Application& app) {
    Surface& surface = *app.surfaces->surfaces[0];
    for (int i = 0; i < event_count; ++i) {
      surface.SetPresentationSize(500 + i, 700 - i);
    }
    CHECK(surface.journal.size() >=
          static_cast<std::size_t>(event_count / 2));
  });
}

void TestLongJournalAddSiblingShutdownSave(int event_count) {
  auto dir = TestDir("apptraverse_surfaces_long_journal_save_profile");
  SurfacesModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    RunSurfacesDemoModelSession(session,
                                [&session](SurfacesPublicationKind) {
                                  session.cv.notify_all();
                                });
  }};
  WaitPublished(session);
  (void)TakeAndWake(session);

  SeedLongJournal(session, event_count);

  auto const add_t0 = std::chrono::steady_clock::now();
  RunOnModel(session, [](Application& app) {
    app.surfaces->surfaces[0]->AddSurface();
    CHECK(app.surfaces->surfaces.size() == 2);
  });
  auto const add_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - add_t0);

  std::cout << "long_journal_save events=" << event_count
            << " add_ms=" << add_ms.count() << '\n';

  auto const stop_t0 = std::chrono::steady_clock::now();
  session.RequestStop();
  model.join();
  auto const stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - stop_t0);
  std::cout << "long_journal_save shutdown_join_ms=" << stop_ms.count()
            << " exit_code=" << session.shutdown_exit_code << '\n';

  CHECK(session.shutdown_exit_code == 0);
  CHECK(stop_ms < std::chrono::seconds{120});

  DirectoryDomainStorage probe{dir};
  probe.BeginSaveSession();
  ae::Domain domain{probe};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 2);
  Surface& surface0 = *application->surfaces->surfaces[0];
  CHECK(surface0.journal.size() >= static_cast<std::size_t>(event_count / 2));
  CHECK(surface0.presentation_width == 500 + (event_count - 1));
  CHECK(surface0.presentation_height == 700 - (event_count - 1));

  {
    SurfacesModelSession session2;
    session2.state_dir = dir;
    std::thread model2{[&] {
      RunSurfacesDemoModelSession(session2,
                                  [&session2](SurfacesPublicationKind) {
                                    session2.cv.notify_all();
                                  });
    }};
    WaitPublished(session2);
    (void)TakeAndWake(session2);
    RunOnModel(session2, [](Application& app) {
      app.surfaces->surfaces[0]->SetPresentationSize(111, 222);
    });
    session2.RequestStop();
    model2.join();
    CHECK(session2.shutdown_exit_code == 0);
  }

  {
    DirectoryDomainStorage storage2{dir};
    ae::Domain domain2{storage2};
    auto reloaded = LoadApplication<Application>(
        domain2, ae::ObjId{surfaces_demo::ToObjId(
                     surfaces_demo::ObjId::Application)});
    CHECK(reloaded->surfaces->surfaces[0]->presentation_width == 111);
    CHECK(reloaded->surfaces->surfaces[0]->presentation_height == 222);
  }

  std::filesystem::remove_all(dir);
}

void TestSaveScaleRamVsDirectory(int event_count) {
  using Clock = std::chrono::steady_clock;

  ae::RamDomainStorage ram;
  ae::Domain ram_domain{ram};
  auto ram_app = BuildSurfacesGraph(ram_domain);
  FinalizeDistilledGraph(*ram_app);
  Surface& ram_surface = *ram_app->surfaces->surfaces[0];
  for (int i = 0; i < event_count; ++i) {
    ram_surface.SetPresentationSize(500 + i, 700 - i);
  }

  auto const ram_save_t0 = Clock::now();
  ram_app.Save();
  auto const ram_save_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                            ram_save_t0);
  auto const ram_destroy_t0 = Clock::now();
  ram_app = {};
  auto const ram_destroy_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                            ram_destroy_t0);

  auto dir = TestDir("apptraverse_surfaces_save_scale_dir");
  DirectoryDomainStorage storage{dir};
  ae::Domain disk_domain{storage};
  auto disk_app = BuildSurfacesGraph(disk_domain);
  FinalizeDistilledGraph(*disk_app);
  SaveDistilledRoot(*disk_app);
  disk_app = {};
  disk_app = LoadApplication<Application>(
      disk_domain, ae::ObjId{surfaces_demo::ToObjId(
                       surfaces_demo::ObjId::Application)});
  Surface& disk_surface = *disk_app->surfaces->surfaces[0];
  for (int i = 0; i < event_count; ++i) {
    disk_surface.SetPresentationSize(500 + i, 700 - i);
  }

  storage.BeginSaveSession();
  auto const disk_save_t0 = Clock::now();
  disk_app.Save();
  auto const disk_save_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                            disk_save_t0);
  DirectoryDomainStorageStoreStats const stats =
      storage.SnapshotSaveSessionStats();
  auto const disk_destroy_t0 = Clock::now();
  disk_app = {};
  auto const disk_destroy_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                            disk_destroy_t0);

  std::cout << "save_scale events=" << event_count << " ram_save_ms="
            << ram_save_ms.count() << " ram_destroy_ms="
            << ram_destroy_ms.count() << " disk_save_ms="
            << disk_save_ms.count() << " disk_destroy_ms="
            << disk_destroy_ms.count() << " disk_layer_store_calls="
            << stats.layer_store_calls << " disk_written_bytes="
            << stats.layers_written_bytes << " disk_compare_read_bytes="
            << stats.layers_compare_bytes_read << " disk_new_file_writes="
            << stats.layers_new_file_writes << " disk_size_mismatch_writes="
            << stats.layers_size_mismatch_writes << '\n';

  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesRegistration();

  for (int n : {100, 1000, 10000}) {
    apptraverse::test::TestSaveScaleRamVsDirectory(n);
  }

  apptraverse::test::TestLongJournalAddSiblingShutdownSave(10000);

  std::cout << "surfaces_long_journal_save_test OK\n";
  return 0;
}
