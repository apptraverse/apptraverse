#include <chrono>
#include <condition_variable>
#include <cstdlib>
#if defined(_WIN32)
#include <stdlib.h>
#endif
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/application/model_checkpoint_persistence.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "apptraverse/platform/windows/surfaces_win_registration.h"

#include "surfaces_distill.h"
#include "surfaces_lifecycle.h"
#include "surfaces_test_loaded_bundle.h"

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

void SetEnvFlag(char const* name, char const* value) {
#if defined(_WIN32)
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
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

struct SurfaceJournalFingerprint {
  std::size_t journal_size{0};
  std::vector<std::uint32_t> event_obj_ids;
  std::vector<std::uint32_t> event_class_ids;
  std::vector<std::int32_t> presentation_widths;
  std::vector<std::int32_t> presentation_heights;

  static SurfaceJournalFingerprint Capture(Surface const& surface) {
    SurfaceJournalFingerprint fp;
    fp.journal_size = surface.journal.size();
    for (EventRecord const& record : surface.journal) {
      CHECK(record.event.is_valid());
      fp.event_obj_ids.push_back(record.event->obj_id.id());
      fp.event_class_ids.push_back(record.event->GetClassId());
      if (SurfacePresentationSizeChangedEvent::ptr size_event{record.event}) {
        fp.presentation_widths.push_back(size_event->width);
        fp.presentation_heights.push_back(size_event->height);
      }
    }
    return fp;
  }

  void ExpectEqual(SurfaceJournalFingerprint const& other) const {
    CHECK(journal_size == other.journal_size);
    CHECK(event_obj_ids == other.event_obj_ids);
    CHECK(event_class_ids == other.event_class_ids);
    CHECK(presentation_widths == other.presentation_widths);
    CHECK(presentation_heights == other.presentation_heights);
  }
};

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

  SurfaceJournalFingerprint before_save;
  RunOnModel(session, [&](Application& app) {
    before_save = SurfaceJournalFingerprint::Capture(
        *app.surfaces->surfaces[0]);
  });

  auto const add_t0 = std::chrono::steady_clock::now();
  RunOnModel(session, [](Application& app) {
    app.surfaces->surfaces[0]->AddSurface();
    CHECK(app.surfaces->surfaces.size() == 2);
  });
  auto const add_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - add_t0);

  std::cout << "long_journal_save events=" << event_count
            << " add_ms=" << add_ms.count() << '\n';
#ifndef NDEBUG
  (void)add_ms;
#else
  if (add_ms >= std::chrono::milliseconds{4000}) {
    std::cerr << "PERF FAIL: long_journal AddSurface ms=" << add_ms.count()
              << " (Release threshold 4000 ms)\n";
    CHECK(false);
  }
#endif

  if (event_count == 10000) {
    SetEnvFlag("APPTRAVERSE_SURFACES_SHUTDOWN_TRACE", "1");
  }
  std::cout << "long_journal_save: requesting stop\n" << std::flush;
  auto const stop_t0 = std::chrono::steady_clock::now();
  session.RequestStop();
  model.join();
  std::cout << "long_journal_save: model joined exit="
            << session.shutdown_exit_code << '\n'
            << std::flush;
  auto const stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - stop_t0);

  std::cout << "long_journal_save: count files\n" << std::flush;
  ModelCheckpointPaths const paths = ModelCheckpointPathsForStateDir(dir);
  auto const file_count = CountRegularFilesUnder(dir);
  auto const checkpoint_bytes = ActiveModelCheckpointByteSize(paths);
  std::cout << "long_journal_save: checks\n" << std::flush;

  std::cout << "long_journal_save shutdown_join_ms=" << stop_ms.count()
            << " exit_code=" << session.shutdown_exit_code
            << " profile_files=" << file_count
            << " checkpoint_bytes=" << checkpoint_bytes << '\n';

  CHECK(session.shutdown_exit_code == 0);
  if (event_count == 10000) {
    SetEnvFlag("APPTRAVERSE_SURFACES_SHUTDOWN_TRACE", "0");
  }
  CHECK(HasActiveModelCheckpoint(paths));
  CHECK(file_count <= 2);
  CHECK(checkpoint_bytes > 0);
  CHECK(stop_ms < std::chrono::seconds{120});

  std::cout << "long_journal_save: cold load\n" << std::flush;
  auto cold = LoadedApplicationBundle::LoadFromModelCheckpoint(dir);
  cold->VerifyOwnerInvariants();
  CHECK(cold->application->surfaces->surfaces.size() == 2);
  SurfaceJournalFingerprint after_cold =
      SurfaceJournalFingerprint::Capture(*cold->application->surfaces->surfaces[0]);
  before_save.ExpectEqual(after_cold);
  CHECK(cold->application->surfaces->surfaces[0]->presentation_width ==
        500 + (event_count - 1));

  std::cout << "long_journal_save: session2 reload\n" << std::flush;
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
    RunOnModel(session2, [&](Application& app) {
      CHECK(app.surfaces->surfaces.size() == 2);
      SurfaceJournalFingerprint after_reload =
          SurfaceJournalFingerprint::Capture(*app.surfaces->surfaces[0]);
      before_save.ExpectEqual(after_reload);
      CHECK(app.surfaces->surfaces[0]->presentation_width ==
            500 + (event_count - 1));
    });
    RunOnModel(session2, [](Application& app) {
      app.surfaces->surfaces[0]->SetPresentationSize(111, 222);
    });
    RunOnModel(session2, [](Application& app) {
      CHECK(app.surfaces->surfaces[0]->presentation_width == 111);
      CHECK(app.surfaces->surfaces[0]->presentation_height == 222);
    });
    session2.RequestStop();
    model2.join();
    CHECK(session2.shutdown_exit_code == 0);
  }

  std::filesystem::remove_all(dir);
}

void TestSaveScaleRamVsDirectoryVsCheckpoint(int event_count) {
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

  auto checkpoint_dir = TestDir("apptraverse_surfaces_save_scale_checkpoint");
  ModelCheckpointPaths checkpoint_paths =
      ModelCheckpointPathsForStateDir(checkpoint_dir);
  auto const checkpoint_t0 = Clock::now();
  CHECK(CommitModelCheckpoint(*ram_app, checkpoint_paths));
  auto const checkpoint_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                            checkpoint_t0);
  CHECK(CountRegularFilesUnder(checkpoint_dir) <= 2);

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
  auto const disk_files = CountRegularFilesUnder(dir);

  std::cout << "save_scale events=" << event_count << " ram_save_ms="
            << ram_save_ms.count() << " checkpoint_ms=" << checkpoint_ms.count()
            << " disk_save_ms=" << disk_save_ms.count()
            << " disk_layer_store_calls=" << stats.layer_store_calls
            << " disk_written_bytes=" << stats.layers_written_bytes
            << " disk_profile_files=" << disk_files << " checkpoint_files="
            << CountRegularFilesUnder(checkpoint_dir) << '\n';

  CHECK(checkpoint_ms < disk_save_ms);
  if (event_count == 10000) {
    CHECK(disk_files > 1000);
    std::cout << "save_scale checkpoint_vs_disk_ratio="
              << (disk_save_ms.count() > 0
                      ? static_cast<double>(checkpoint_ms.count()) /
                            static_cast<double>(disk_save_ms.count())
                      : 0.0)
              << '\n';
  }

  std::filesystem::remove_all(dir);
  std::filesystem::remove_all(checkpoint_dir);
}

void ProfilePackedCheckpointPhases(int event_count) {
  using Clock = std::chrono::steady_clock;
  auto const ms = [](Clock::duration d) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
  };

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  Application::ptr application = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*application);
  Surface& surface = *application->surfaces->surfaces[0];
  for (int i = 0; i < event_count; ++i) {
    surface.SetPresentationSize(500 + i, 700 - i);
  }

  auto const ram_t0 = Clock::now();
  application.Save();
  auto const ram_save_ms = ms(Clock::now() - ram_t0);

  SetEnvFlag("APPTRAVERSE_PERSISTENT_SNAPSHOT_PROFILE", "1");
  SetEnvFlag("APPTRAVERSE_CHECKPOINT_COMMIT_PROFILE", "1");
  ByteSink snapshot_bytes;
  auto const serialize_t0 = Clock::now();
  SerializePersistentModelSnapshot(*application, snapshot_bytes);
  auto const serialize_only_ms = ms(Clock::now() - serialize_t0);

  auto const profile_dir = TestDir("apptraverse_surfaces_checkpoint_profile");
  ModelCheckpointPaths paths = ModelCheckpointPathsForStateDir(profile_dir);
  auto const commit_t0 = Clock::now();
  CHECK(CommitModelCheckpoint(*application, paths));
  auto const commit_ms = ms(Clock::now() - commit_t0);

  auto const release_t0 = Clock::now();
  application = {};
  auto const root_release_ms = ms(Clock::now() - release_t0);

  std::cout << "packed_checkpoint_profile events=" << event_count
            << " ram_save_ms=" << ram_save_ms
            << " serialize_only_ms=" << serialize_only_ms
            << " commit_total_ms=" << commit_ms
            << " root_release_ms=" << root_release_ms
            << " snapshot_bytes=" << snapshot_bytes.bytes.size()
            << " checkpoint_bytes="
            << ActiveModelCheckpointByteSize(paths) << '\n';

  SetEnvFlag("APPTRAVERSE_PERSISTENT_SNAPSHOT_PROFILE", "0");
  SetEnvFlag("APPTRAVERSE_CHECKPOINT_COMMIT_PROFILE", "0");
  std::filesystem::remove_all(profile_dir);
}

void TestCheckpointCommitFailureKeepsPriorFile() {
  auto dir = TestDir("apptraverse_surfaces_checkpoint_atomic");
  ae::RamDomainStorage ram;
  ae::Domain domain{ram};
  auto app = BuildSurfacesGraph(domain);
  FinalizeDistilledGraph(*app);
  ModelCheckpointPaths paths = ModelCheckpointPathsForStateDir(dir);
  std::cout << "atomic: commit\n" << std::flush;
  CHECK(CommitModelCheckpoint(*app, paths));
  auto const first_size = ActiveModelCheckpointByteSize(paths);
  std::cout << "atomic: ok\n" << std::flush;

  paths.temp = dir / "blocked.tmp";
  std::error_code ec;
  std::filesystem::create_directories(dir / "blocked.tmp", ec);
  CHECK(!CommitModelCheckpoint(*app, paths));
  CHECK(ActiveModelCheckpointByteSize(paths) == first_size);

  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  std::cout << "long_journal_save: start\n" << std::flush;
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesRegistration();
  apptraverse::EnsureWin32SurfacesPresenterRegistration();

  std::cout << "long_journal_save: atomic test\n" << std::flush;
  apptraverse::test::TestCheckpointCommitFailureKeepsPriorFile();
  std::cout << "long_journal_save: checkpoint phase profile\n" << std::flush;
  for (int n : {100, 1000, 10000}) {
    apptraverse::test::ProfilePackedCheckpointPhases(n);
  }
  std::cout << "long_journal_save: session test\n" << std::flush;
  apptraverse::test::TestLongJournalAddSiblingShutdownSave(10000);
  std::cout << "long_journal_save: scale tests\n" << std::flush;
  for (int n : {100, 1000}) {
    apptraverse::test::TestSaveScaleRamVsDirectoryVsCheckpoint(n);
  }

  std::cout << "surfaces_long_journal_save_test OK\n";
  return 0;
}
