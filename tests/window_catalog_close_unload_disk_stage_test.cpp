#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/overlay_domain_storage.h"

#include "embedded_distilled_window_catalog_supply.h"
#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"
#include "window_catalog_supply_constant_ids.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(2);                                                          \
    }                                                                        \
  } while (false)

using Clock = std::chrono::steady_clock;

int64_t MsBetween(Clock::time_point const& a, Clock::time_point const& b) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
}

std::filesystem::path TestDir(char const* name) {
  auto const dir =
      std::filesystem::temp_directory_path() / "apptraverse_test" / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

WindowHolder* AsHolder(ui::UiListRow::ptr const& row) {
  return static_cast<WindowHolder*>(row.operator->());
}

InstrumentedDomainStorageStats DeltaStats(
    InstrumentedDomainStorageStats const& after,
    InstrumentedDomainStorageStats const& before) {
  InstrumentedDomainStorageStats d{};
  d.store_calls = after.store_calls - before.store_calls;
  d.store_disk_bytes_written =
      after.store_disk_bytes_written - before.store_disk_bytes_written;
  d.store_bytes_accepted =
      after.store_bytes_accepted - before.store_bytes_accepted;
  return d;
}

struct PublicationWaiter {
  std::mutex mu;
  std::condition_variable cv;
  int pending{0};
  ApplicationPublicationKind kind{ApplicationPublicationKind::Initial};
  std::vector<std::uint8_t> bytes;
};

void WaitPublication(PublicationWaiter& waiter) {
  std::unique_lock<std::mutex> lock{waiter.mu};
  waiter.cv.wait(lock, [&] { return waiter.pending > 0; });
  --waiter.pending;
}

void BurstResizeChild(CatalogWindow& window) {
  for (int i = 0; i < 31; ++i) {
    std::int32_t const w = (i % 2 == 0) ? 900 : 500;
    std::int32_t const ch = (i % 2 == 0) ? 500 : 900;
    window.SubmitFrameFromNative(100, 100, w, ch, false);
  }
}

void LogModelCloseLine(char const* label, int64_t close_ms,
                       InstrumentedDomainStorageStats const& d,
                       DirectoryDomainStorageStoreStats const& disk,
                       std::size_t pub_bytes) {
  std::cout << "CLOSE_STAGE " << label << " model_close_ms=" << close_ms
            << " store_calls=" << d.store_calls
            << " disk_layers_written=" << disk.layers_written
            << " disk_layers_skipped=" << disk.layers_skipped_unchanged
            << " disk_bytes_written=" << disk.layers_written_bytes
            << " store_accepted_bytes=" << d.store_bytes_accepted
            << " publication_bytes=" << pub_bytes << '\n';
}

void TestModelOnlyCloseStorage(bool burst_resize, bool ram_user = false) {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::window_catalog::kEmbeddedGraph);
  auto const user_dir =
      TestDir(burst_resize ? "wc_close_stage_burst_user"
                           : "wc_close_stage_cold_user");
  DirectoryDomainStorage user_storage{user_dir};
  ae::RamDomainStorage ram_storage;
  ae::IDomainStorage& user = ram_user
      ? static_cast<ae::IDomainStorage&>(ram_storage)
      : static_cast<ae::IDomainStorage&>(user_storage);
  OverlayDomainStorage overlay{
      user, backing,
      EmbeddedSupplyConstantIdSet(
          embedded_distilled::window_catalog::kEmbeddedGraph)};
  InstrumentedDomainStorage instrumented{overlay};

  ae::Domain domain{instrumented};
  auto catalog = LoadApplication<WindowCatalog>(domain, catalog_id);
  PrepareWindowCatalogForRun(*catalog);
  catalog->AddWindow();
  catalog->persisted.list.Load();
  WindowHolder* holder = AsHolder(catalog->persisted.list->rows[0]);
  holder->window.Load();
  if (burst_resize) {
    BurstResizeChild(*holder->window);
  }
  InstrumentedDomainStorageStats const before = instrumented.SnapshotStats();
  user_storage.BeginSaveSession();
  auto const t0 = Clock::now();
  catalog->CloseHolderWindow(*holder);
  auto const t1 = Clock::now();
  InstrumentedDomainStorageStats const after = instrumented.SnapshotStats();
  CHECK(!holder->window.is_loaded());
  CHECK(holder->window.is_valid());
  char const* label = ram_user ? (burst_resize ? "RAM_BURST" : "RAM_COLD")
                               : (burst_resize ? "MODEL_BURST" : "MODEL_COLD");
  LogModelCloseLine(label,
                    MsBetween(t0, t1), DeltaStats(after, before),
                    user_storage.SnapshotSaveSessionStats(), 0);
  CHECK(!user_storage.SaveSessionHadFailure());
}

void TestPublicationPipelineCloseOnDisk(bool burst_resize) {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::window_catalog::kEmbeddedGraph);
  auto const user_dir =
      TestDir(burst_resize ? "wc_close_stage_pub_burst"
                           : "wc_close_stage_pub_cold");
  DirectoryDomainStorage user_storage{user_dir};
  OverlayDomainStorage overlay{
      user_storage, backing,
      EmbeddedSupplyConstantIdSet(
          embedded_distilled::window_catalog::kEmbeddedGraph)};
  InstrumentedDomainStorage instrumented{overlay};

  ApplicationModelRuntime runtime;
  PublicationWaiter waiter;
  int session_result = 1;
  ModelObjectProxy model_proxy{[&runtime](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  }};

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.persist = [](WindowCatalog&) -> int { return 0; };

  std::thread model_thread{[&] {
    session_result = RunApplicationModelSession(
        instrumented, catalog_id, runtime,
        [&](ApplicationPublicationKind kind) {
          std::vector<std::uint8_t> bytes = runtime.TakePublishedCopy();
          runtime.NotifyPublicationConsumed();
          std::lock_guard<std::mutex> lock{waiter.mu};
          waiter.kind = kind;
          waiter.bytes = std::move(bytes);
          ++waiter.pending;
          waiter.cv.notify_one();
        },
        hooks);
  }};

  WaitPublication(waiter);
  CHECK(waiter.kind == ApplicationPublicationKind::Initial);
  ApplicationUiSession ui_session{waiter.bytes, nullptr, model_proxy};
  WindowCatalog& ui_catalog =
      static_cast<WindowCatalog&>(ui_session.root());

  auto apply_incremental = [&] {
    WaitPublication(waiter);
    CHECK(waiter.kind == ApplicationPublicationKind::Incremental);
    ui_session.ApplyStructural(waiter.bytes);
  };

  model_proxy.Invoke(ui_catalog.obj_id, &WindowCatalog::AddWindow);
  apply_incremental();
  ui_catalog.persisted.list.Load();
  WindowHolder* holder = AsHolder(ui_catalog.persisted.list->rows[0]);
  holder->window.Load();
  ae::ObjId const window_id = holder->window.id();
  if (burst_resize) {
    BurstResizeChild(*holder->window);
    apply_incremental();
  }

  InstrumentedDomainStorageStats const before = instrumented.SnapshotStats();
  auto const t0 = Clock::now();
  model_proxy.Invoke(ui_catalog.obj_id, &WindowCatalog::UserCloseWindow,
                     window_id);
  auto const t_post = Clock::now();
  WaitPublication(waiter);
  auto const t_pub = Clock::now();
  InstrumentedDomainStorageStats const mid = instrumented.SnapshotStats();
  auto const t_apply0 = Clock::now();
  ui_session.ApplyStructural(waiter.bytes);
  auto const t_apply1 = Clock::now();
  InstrumentedDomainStorageStats const after = instrumented.SnapshotStats();

  runtime.RequestStop();
  model_thread.join();
  CHECK(session_result == 0);
  CHECK(!holder->window.is_loaded());

  std::cout << "CLOSE_STAGE " << (burst_resize ? "PIPELINE_BURST" : "PIPELINE_COLD")
            << " enqueue_to_pub_ms=" << MsBetween(t0, t_pub)
            << " apply_structural_ms=" << MsBetween(t_apply0, t_apply1)
            << " store_calls_model=" << (mid.store_calls - before.store_calls)
            << " store_disk_bytes_model="
            << (mid.store_disk_bytes_written - before.store_disk_bytes_written)
            << " publication_bytes=" << waiter.bytes.size() << '\n';
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureWindowCatalogGraphRegistration();
  apptraverse::test::TestModelOnlyCloseStorage(false);
  apptraverse::test::TestModelOnlyCloseStorage(true);
  apptraverse::test::TestModelOnlyCloseStorage(false, true);
  apptraverse::test::TestModelOnlyCloseStorage(true, true);
  std::cout << "window_catalog_close_unload_disk_stage_test OK\n";
  return 0;
}
