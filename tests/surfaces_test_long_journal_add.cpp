#include "surfaces_test_long_journal_add.h"

#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"

#include "apptraverse/application/model_publication_session.h"

namespace apptraverse::test {
namespace {

#define LJ_CHECK(cond)                                                         \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"          \
                << __LINE__ << '\n';                                           \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

using Clock = std::chrono::steady_clock;

Application& ModelApplication(ae::Domain& domain) {
  auto object = domain.Find(ae::ObjId{surfaces_demo::ToObjId(
      surfaces_demo::ObjId::Application)});
  LJ_CHECK(object);
  return static_cast<Application&>(*object);
}

void TakeAndWake(SurfacesModelSession& session) {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session.mu};
    bytes = session.channel.TakePublishedCopy();
  }
  session.cv.notify_all();
  LJ_CHECK(!bytes.empty());
}

#ifdef NDEBUG
constexpr auto kModelWatchdog = std::chrono::seconds{30};
#else
constexpr auto kModelWatchdog = std::chrono::seconds{120};
#endif

template <typename Body>
LongJournalAddTiming RunOnModelMeasuredSplit(SurfacesModelSession& session,
                                             Body body) {
  LongJournalAddTiming timing;
  std::promise<void> done;
  auto future = done.get_future();
  auto const post_time = Clock::now();
  session.Post([&, body = std::move(body)](ae::Domain& domain) mutable {
    auto const task_start = Clock::now();
    Application& app = ModelApplication(domain);
    auto const body_start = Clock::now();
    body(app);
    auto const body_end = Clock::now();
    (void)app;
    auto const task_end = Clock::now();
    timing.queue_wait_us =
        std::chrono::duration_cast<std::chrono::microseconds>(body_start -
                                                              post_time)
            .count();
    timing.add_body_us =
        std::chrono::duration_cast<std::chrono::microseconds>(body_end -
                                                              body_start)
            .count();
    timing.task_cleanup_us =
        std::chrono::duration_cast<std::chrono::microseconds>(task_end -
                                                              body_end)
            .count();
    done.set_value();
  });
  LJ_CHECK(future.wait_for(kModelWatchdog) == std::future_status::ready);
  auto const future_ready = Clock::now();
  timing.client_wait_us =
      std::chrono::duration_cast<std::chrono::microseconds>(future_ready -
                                                            post_time)
          .count();
  return timing;
}

}  // namespace

SurfaceJournalFingerprint SurfaceJournalFingerprint::Capture(
    Surface const& surface) {
  SurfaceJournalFingerprint fp;
  fp.journal_size = surface.journal.size();
  for (EventRecord const& record : surface.journal) {
    LJ_CHECK(record.event.is_valid());
    fp.event_obj_ids.push_back(record.event->obj_id.id());
    fp.event_class_ids.push_back(record.event->GetClassId());
    if (SurfacePresentationSizeChangedEvent::ptr size_event{record.event}) {
      fp.presentation_widths.push_back(size_event->width);
      fp.presentation_heights.push_back(size_event->height);
    }
  }
  return fp;
}

void SurfaceJournalFingerprint::ExpectEqual(
    SurfaceJournalFingerprint const& other) const {
  LJ_CHECK(journal_size == other.journal_size);
  LJ_CHECK(event_obj_ids == other.event_obj_ids);
  LJ_CHECK(event_class_ids == other.event_class_ids);
  LJ_CHECK(presentation_widths == other.presentation_widths);
  LJ_CHECK(presentation_heights == other.presentation_heights);
}

void SeedLongJournalOnModel(SurfacesModelSession& session, int event_count) {
  RunOnModelMeasuredSplit(session, [event_count](Application& app) {
    Surface& surface = *app.surfaces->surfaces[0];
    for (int i = 0; i < event_count; ++i) {
      surface.SetPresentationSize(500 + i, 700 - i);
    }
    LJ_CHECK(surface.journal.size() >=
             static_cast<std::size_t>(event_count / 2));
  });
}

void WaitModelPublicationBacklogDrained(SurfacesModelSession& session) {
  std::uint64_t last_publish_count = session.channel.publish_count();
  int stable_passes = 0;
  while (stable_passes < 4) {
    while (session.channel.has_unread_published()) {
      TakeAndWake(session);
      stable_passes = 0;
    }
    RunOnModelMeasuredSplit(session, [](Application&) {});
    std::uint64_t const publish_count = session.channel.publish_count();
    if (publish_count == last_publish_count) {
      ++stable_passes;
    } else {
      last_publish_count = publish_count;
      stable_passes = 0;
    }
  }
}

SurfaceJournalFingerprint CaptureLongJournalFingerprintOnModel(
    SurfacesModelSession& session) {
  SurfaceJournalFingerprint captured;
  RunOnModelMeasuredSplit(session, [&](Application& app) {
    captured = SurfaceJournalFingerprint::Capture(*app.surfaces->surfaces[0]);
  });
  return captured;
}

LongJournalAddTiming RunMeasuredAddSurfaceOnFirstSurface(
    SurfacesModelSession& session) {
  return RunOnModelMeasuredSplit(session, [](Application& app) {
    app.surfaces->surfaces[0]->AddSurface();
    LJ_CHECK(app.surfaces->surfaces.size() == 2);
  });
}

LongJournalAddPerfResult RunLongJournalAddSiblingPerfScenario(
    SurfacesModelSession& session, int event_count) {
  LongJournalAddPerfResult result;
  SeedLongJournalOnModel(session, event_count);
  WaitModelPublicationBacklogDrained(session);
  result.fingerprint_before_add = CaptureLongJournalFingerprintOnModel(session);
  result.journal_size_after_seed = result.fingerprint_before_add.journal_size;
  result.timing = RunMeasuredAddSurfaceOnFirstSurface(session);
  return result;
}

void ReportLongJournalAddPerf(char const* label,
                              LongJournalAddPerfResult const& result) {
  auto const& t = result.timing;
  std::cout << "long_journal_add_perf label=" << label
            << " journal_size=" << result.journal_size_after_seed
            << " client_wait_ms=" << (t.client_wait_us / 1000)
            << " queue_wait_ms=" << (t.queue_wait_us / 1000)
            << " add_body_ms=" << (t.add_body_us / 1000)
            << " task_cleanup_ms=" << (t.task_cleanup_us / 1000) << '\n';
}

void AssertReleaseLongJournalAddPerfGate(char const* label,
                                         LongJournalAddPerfResult const& result) {
#ifndef NDEBUG
  (void)label;
  (void)result;
#else
  if (result.timing.client_wait_us >= 4'000'000) {
    std::cerr << "PERF FAIL: " << label << " client_wait_ms="
              << (result.timing.client_wait_us / 1000)
              << " (Release threshold 4000 ms)"
              << " queue_wait_ms=" << (result.timing.queue_wait_us / 1000)
              << " add_body_ms=" << (result.timing.add_body_us / 1000)
              << " task_cleanup_ms=" << (result.timing.task_cleanup_us / 1000)
              << '\n';
    LJ_CHECK(false);
  }
#endif
}

}  // namespace apptraverse::test
