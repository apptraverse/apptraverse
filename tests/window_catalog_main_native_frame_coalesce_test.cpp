#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/overlay_domain_storage.h"

#include "embedded_distilled_window_catalog_supply.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_main_native_frame_queue.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"

namespace apptraverse::test {
namespace {
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

std::filesystem::path TestDir(char const* name) {
  auto const dir =
      std::filesystem::temp_directory_path() / "apptraverse_test" / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

struct PublicationWaiter {
  std::mutex mu;
  std::condition_variable cv;
  int pending{0};
  ApplicationPublicationKind kind{ApplicationPublicationKind::Initial};
};

void WaitPublication(PublicationWaiter& waiter) {
  std::unique_lock<std::mutex> lock{waiter.mu};
  waiter.cv.wait(lock, [&] { return waiter.pending > 0; });
  --waiter.pending;
}

void TestCoalescedQueuePostsSingleModelWork() {
  WindowCatalogMainNativeFrameQueue queue;
  std::deque<ModelObjectProxy::ModelWork> posted;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  }};
  ae::ObjId const catalog_id{1};
  ae::ObjId const window_id{2};
  for (int i = 0; i < 64; ++i) {
    PostWindowCatalogMainNativeFrame(queue, &proxy, catalog_id, window_id, 10,
                                     20, 500 + i, 400, false);
  }
  CHECK(posted.size() == 1);
}

void TestIndependentSessionsApplyDistinctFrames() {
  ae::RamDomainStorage storage_a;
  ae::RamDomainStorage storage_b;
  ae::Domain domain_a{storage_a};
  ae::Domain domain_b{storage_b};
  auto catalog_a = BuildWindowCatalogGraph(domain_a);
  auto catalog_b = BuildWindowCatalogGraph(domain_b);
  PrepareWindowCatalogForRun(*catalog_a);
  PrepareWindowCatalogForRun(*catalog_b);
  ae::ObjId const catalog_id = catalog_a->obj_id;
  ae::ObjId const window_id_a = catalog_a->persisted.main_window.id();
  ae::ObjId const window_id_b = catalog_b->persisted.main_window.id();
  CHECK(window_id_a == window_id_b);

  WindowCatalogMainNativeFrameQueue queue_a;
  WindowCatalogMainNativeFrameQueue queue_b;
  std::deque<ModelObjectProxy::ModelWork> posted_a;
  std::deque<ModelObjectProxy::ModelWork> posted_b;
  queue_a.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted_a.push_back(std::move(work));
  });
  queue_b.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted_b.push_back(std::move(work));
  });
  ModelObjectProxy proxy_a{[&](ModelObjectProxy::ModelWork) {}};
  ModelObjectProxy proxy_b{[&](ModelObjectProxy::ModelWork) {}};

  for (int i = 0; i < 8; ++i) {
    PostWindowCatalogMainNativeFrame(queue_a, &proxy_a, catalog_id, window_id_a,
                                     0, 0, 640 + i, 480, false);
    PostWindowCatalogMainNativeFrame(queue_b, &proxy_b, catalog_id, window_id_b,
                                     0, 0, 320 + i, 900, false);
  }
  CHECK(posted_a.size() == 1);
  CHECK(posted_b.size() == 1);
  posted_a.front()(domain_a);
  posted_b.front()(domain_b);
  CHECK(catalog_a->persisted.main_window->client_width() == 647);
  CHECK(catalog_a->persisted.main_client_viewport.height == 480);
  CHECK(catalog_b->persisted.main_window->client_width() == 327);
  CHECK(catalog_b->persisted.main_client_viewport.height == 900);
  queue_a.ClosePosting();
  queue_b.ClosePosting();
}

void TestCoalescedDrainAppliesFrameAndViewportTogether() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  PrepareWindowCatalogForRun(*catalog);
  ae::ObjId const catalog_id = catalog->obj_id;
  ae::ObjId const main_id = catalog->persisted.main_window.id();

  WindowCatalogMainNativeFrameQueue queue;
  std::deque<ModelObjectProxy::ModelWork> posted;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    posted.push_back(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork) {}};
  PostWindowCatalogMainNativeFrame(queue, &proxy, catalog_id, main_id, 80, 80,
                                   520, 880, false);
  CHECK(posted.size() == 1);
  posted.front()(domain);
  CHECK(catalog->persisted.main_window->client_width() == 520);
  CHECK(catalog->persisted.main_client_viewport.width == 520);
  CHECK(catalog->persisted.main_client_viewport.height == 880);
  queue.ClosePosting();
}


void TestRestoreFrameSurvivesLaterLiveReports() {
  struct Report {
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
    bool restore;
  };
  Report const first{10, 20, 400, 300, true};
  Report const final{-231, -143, 701, 503, true};
  Report const close{-231, -143, 701, 503, false};
  Report const maximized{-8, -8, 1280, 900, false};
  struct Scenario {
    std::vector<Report> reports;
    Report expected_restore;
    Report expected_live;
  };
  for (auto const& scenario : std::vector<Scenario>{
           {{final}, final, final},
           {{final, close}, final, close},
           {{final, maximized}, final, maximized},
           {{first, maximized, final, maximized}, final, maximized}}) {
    ae::RamDomainStorage storage;
    ae::Domain domain{storage};
    auto catalog = BuildWindowCatalogGraph(domain);
    PrepareWindowCatalogForRun(*catalog);
    auto window = catalog->persisted.main_window;
    WindowCatalogMainNativeFrameQueue queue;
    std::deque<ModelObjectProxy::ModelWork> posted;
    queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
      posted.push_back(std::move(work));
    });
    ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork) {}};
    for (auto const& report : scenario.reports) {
      queue.Post(&proxy, catalog->obj_id, window.id(), report.x, report.y,
                 report.width, report.height, report.restore);
    }
    CHECK(posted.size() == 1);
    auto work = std::move(posted.front());
    posted.pop_front();
    work(domain);
    CHECK(posted.empty());
    CHECK(window->x() == scenario.expected_restore.x);
    CHECK(window->y() == scenario.expected_restore.y);
    CHECK(window->restore_client_width() == scenario.expected_restore.width);
    CHECK(window->restore_client_height() == scenario.expected_restore.height);
    CHECK(window->client_width() == scenario.expected_live.width);
    CHECK(window->client_height() == scenario.expected_live.height);
    CHECK(catalog->persisted.main_client_viewport.width == scenario.expected_live.width);
    CHECK(catalog->persisted.main_client_viewport.height == scenario.expected_live.height);
    queue.ClosePosting();
  }
}

void TestReportsDuringDrainStayInAcceptedTask() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  PrepareWindowCatalogForRun(*catalog);
  auto window = catalog->persisted.main_window;
  WindowCatalogMainNativeFrameQueue queue;
  std::deque<ModelObjectProxy::ModelWork> posted;
  std::size_t task_count = 0;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    ++task_count;
    posted.push_back(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork) {}};
  struct DuringApply {
    WindowCatalogMainNativeFrameQueue& queue;
    ModelObjectProxy& proxy;
    ae::ObjId catalog_id;
    ae::ObjId window_id;
    bool posted{false};
  } context{queue, proxy, catalog->obj_id, window.id()};
  window->BindMaterializedChangeNotifier(
      &context, [](void* opaque, Node&) {
        auto& ctx = *static_cast<DuringApply*>(opaque);
        if (ctx.posted) {
          return;
        }
        ctx.posted = true;
        ctx.queue.Post(&ctx.proxy, ctx.catalog_id, ctx.window_id,
                        -211, -123, 812, 610, true);
        ctx.queue.Post(&ctx.proxy, ctx.catalog_id, ctx.window_id,
                        -8, -8, 1280, 900, false);
      });
  queue.Post(&proxy, catalog->obj_id, window.id(), 12, 34, 700, 500, true);
  CHECK(posted.size() == 1);
  auto work = std::move(posted.front());
  posted.pop_front();
  work(domain);
  window->ClearMaterializedChangeNotifier();
  CHECK(context.posted);
  CHECK(task_count == 1);
  CHECK(posted.empty());
  CHECK(window->x() == -211);
  CHECK(window->y() == -123);
  CHECK(window->restore_client_width() == 812);
  CHECK(window->restore_client_height() == 610);
  CHECK(window->client_width() == 1280);
  CHECK(window->client_height() == 900);
  CHECK(catalog->persisted.main_client_viewport.width == 1280);
  CHECK(catalog->persisted.main_client_viewport.height == 900);

  // A subsequent live-only drain must not replay the consumed restore report.
  window->SubmitFrameFromNative(-70, -80, 600, 400, true);
  queue.Post(&proxy, catalog->obj_id, window.id(), -8, -8, 1440, 960, false);
  CHECK(task_count == 2);
  CHECK(posted.size() == 1);
  work = std::move(posted.front());
  posted.pop_front();
  work(domain);
  CHECK(posted.empty());
  CHECK(window->x() == -70);
  CHECK(window->y() == -80);
  CHECK(window->restore_client_width() == 600);
  CHECK(window->restore_client_height() == 400);
  CHECK(window->client_width() == 1440);
  CHECK(window->client_height() == 960);
  queue.ClosePosting();
}

void TestShutdownResizePersistAndColdLoad() {
  auto const& graph = embedded_distilled::window_catalog::kEmbeddedGraph;
  ae::ObjId const catalog_id{graph.root_object_id};
  ae::ObjId const main_window_id{window_catalog_demo::ToObjId(
      window_catalog_demo::ObjId::MainWindow)};
  auto const profile_dir = TestDir("wc_frame_shutdown_cold_embedded");

  ae::RamDomainStorage model_storage;
  ApplicationModelRuntime runtime;
  WindowCatalogMainNativeFrameQueue queue;
  queue.BindPostWork([&](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  });
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  }};

  PublicationWaiter waiter;
  struct HoldSync {
    std::mutex mu;
    std::condition_variable cv;
    bool hold_worker_live{false};
    bool release_model{false};
  };
  HoldSync hold;
  int session_result = 1;

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = [&](WindowCatalog& catalog) {
    PrepareWindowCatalogForRun(catalog);
  };
  hooks.persist = [&](WindowCatalog& catalog) -> int {
    SaveWindowCatalogPersistentSnapshot(catalog);
    bool const saved = SaveUserStorageFile(model_storage, profile_dir);
    CHECK(saved);
    return 0;
  };

  std::thread model_thread{[&] {
    session_result = RunEmbeddedApplicationModelSession(
        model_storage, graph, runtime,
        [&](ApplicationPublicationKind kind) {
          runtime.TakePublishedCopy();
          runtime.NotifyPublicationConsumed();
          {
            std::lock_guard<std::mutex> lock{waiter.mu};
            waiter.kind = kind;
            ++waiter.pending;
          }
          waiter.cv.notify_one();
        },
        hooks);
  }};

  WaitPublication(waiter);
  CHECK(waiter.kind == ApplicationPublicationKind::Initial);

  runtime.Post([&](ae::Domain&) {
    {
      std::lock_guard<std::mutex> lock{hold.mu};
      hold.hold_worker_live = true;
    }
    hold.cv.notify_all();
    std::unique_lock<std::mutex> lock{hold.mu};
    hold.cv.wait(lock, [&] { return hold.release_model; });
  });

  {
    std::unique_lock<std::mutex> lock{hold.mu};
    hold.cv.wait(lock, [&] { return hold.hold_worker_live; });
  }

  PostWindowCatalogMainNativeFrame(queue, &proxy, catalog_id, main_window_id, -112,
                                   34, 812, 610, true);
  PostWindowCatalogMainNativeFrame(queue, &proxy, catalog_id, main_window_id, -8,
                                   -8, 1280, 900, false);
  runtime.RequestStop();
  {
    std::lock_guard<std::mutex> lock{hold.mu};
    hold.release_model = true;
  }
  hold.cv.notify_all();
  model_thread.join();
  CHECK(session_result == 0);
  queue.ClosePosting();

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, graph);
  ae::RamDomainStorage user_storage;
  bool const loaded = LoadUserStorageFile(user_storage, profile_dir);
  CHECK(loaded);
  OverlayDomainStorage overlay{
      user_storage, backing, EmbeddedSupplyConstantIdSet(graph)};
  ae::Domain verify_domain{overlay};
  auto catalog = LoadApplication<WindowCatalog>(verify_domain, catalog_id);
  CHECK(catalog->obj_id == catalog_id);
  CHECK(catalog_id ==
        window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog));
  CHECK(main_window_id ==
        window_catalog_demo::ToObjId(window_catalog_demo::ObjId::MainWindow));
  catalog->persisted.main_window.Load();
  CHECK(catalog->persisted.main_window->x() == -112);
  CHECK(catalog->persisted.main_window->y() == 34);
  CHECK(catalog->persisted.main_window->restore_client_width() == 812);
  CHECK(catalog->persisted.main_window->restore_client_height() == 610);
  CHECK(catalog->persisted.main_window->client_width() == 1280);
  CHECK(catalog->persisted.main_window->client_height() == 900);
  CHECK(catalog->persisted.main_client_viewport.width == 1280);
  CHECK(catalog->persisted.main_client_viewport.height == 900);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureWindowCatalogGraphRegistration();
  apptraverse::test::TestIndependentSessionsApplyDistinctFrames();
  apptraverse::test::TestCoalescedQueuePostsSingleModelWork();
  apptraverse::test::TestCoalescedDrainAppliesFrameAndViewportTogether();
  apptraverse::test::TestRestoreFrameSurvivesLaterLiveReports();
  apptraverse::test::TestReportsDuringDrainStayInAcceptedTask();
  apptraverse::test::TestShutdownResizePersistAndColdLoad();
  std::cout << "window_catalog_main_native_frame_coalesce_test OK\n";
  return 0;
}
