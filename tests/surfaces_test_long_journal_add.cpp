#include "surfaces_test_long_journal_add.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>

#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"

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

std::int64_t Us(Clock::duration d) {
  return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}

LongJournalAddTiming RunMeasuredAddSurfaceOnFirstSurfaceImpl(
    SurfacesModelSession& session) {
  LongJournalAddTiming timing;
  timing.publish_count_at_post = session.channel.publish_count();
  timing.channel_busy_at_post = session.channel.is_publication_busy();

  std::promise<void> done;
  auto future = done.get_future();
  auto const post_time = Clock::now();

  session.Post([&session, &timing, &done, post_time](ae::Domain& domain) {
    auto const lambda_start = Clock::now();
    timing.queue_wait_us = Us(lambda_start - post_time);
    timing.publish_count_at_task_start = session.channel.publish_count();

    auto const root_start = Clock::now();
    Application& app = ModelApplication(domain);
    auto const root_end = Clock::now();
    timing.root_access_us = Us(root_end - root_start);

    auto const add_start = Clock::now();
    app.surfaces->surfaces[0]->AddSurface();
    LJ_CHECK(app.surfaces->surfaces.size() == 2);
    auto const add_end = Clock::now();
    timing.add_body_us = Us(add_end - add_start);

    (void)app;
    auto const task_end = Clock::now();
    timing.cleanup_us = Us(task_end - add_end);
    done.set_value();
  });

  LJ_CHECK(future.wait_for(kModelWatchdog) == std::future_status::ready);
  auto const future_ready = Clock::now();
  timing.client_wait_us = Us(future_ready - post_time);
  return timing;
}

template <typename Body>
void RunOnModelPlain(SurfacesModelSession& session, Body body) {
  std::promise<void> done;
  auto future = done.get_future();
  session.Post([&](ae::Domain& domain) {
    body(ModelApplication(domain));
    done.set_value();
  });
  LJ_CHECK(future.wait_for(kModelWatchdog) == std::future_status::ready);
}

bool PendingWorkEmpty(SurfacesModelSession& session) {
  std::lock_guard<std::mutex> lock{session.mu};
  return session.pending_work.empty();
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

LongJournalSeedStats SeedLongJournalOnModel(SurfacesModelSession& session,
                                            int event_count) {
  LongJournalSeedStats stats;
  stats.publish_count_before = session.channel.publish_count();
  RunOnModelPlain(session, [&](Application& app) {
    Surface& surface = *app.surfaces->surfaces[0];
    for (int i = 0; i < event_count; ++i) {
      surface.SetPresentationSize(500 + i, 700 - i);
    }
    LJ_CHECK(surface.journal.size() >=
             static_cast<std::size_t>(event_count / 2));
    stats.journal_event_count = surface.journal.size();
  });
  stats.publish_count_after = session.channel.publish_count();
  stats.channel_busy_after_seed = session.channel.is_publication_busy();
  return stats;
}

void WaitModelPublicationBacklogDrained(SurfacesModelSession& session) {
  int idle_passes = 0;
  while (idle_passes < 6) {
    while (session.channel.has_unread_published()) {
      TakeAndWake(session);
      idle_passes = 0;
    }
    std::uint64_t const before = session.channel.publish_count();
    RunOnModelPlain(session, [](Application&) {});
    while (session.channel.has_unread_published()) {
      TakeAndWake(session);
      idle_passes = 0;
    }
    std::uint64_t const after = session.channel.publish_count();
    if (after == before && PendingWorkEmpty(session) &&
        !session.channel.has_unread_published() &&
        !session.channel.is_publication_busy()) {
      ++idle_passes;
    } else {
      idle_passes = 0;
    }
  }
}

LongJournalAddTiming RunMeasuredAddSurfaceOnFirstSurface(
    SurfacesModelSession& session) {
  return RunMeasuredAddSurfaceOnFirstSurfaceImpl(session);
}

LongJournalAddPerfResult RunAddImmediatelyAfterChanges(
    SurfacesModelSession& session, int event_count) {
  LongJournalAddPerfResult result;
  result.seed = SeedLongJournalOnModel(session, event_count);
  result.journal_size_after_seed = result.seed.journal_event_count;
  result.timing = RunMeasuredAddSurfaceOnFirstSurfaceImpl(session);
  RunOnModelPlain(session, [&](Application& app) {
    result.fingerprint =
        SurfaceJournalFingerprint::Capture(*app.surfaces->surfaces[0]);
  });
  return result;
}

LongJournalAddPerfResult RunAddAfterPublicationDrain(
    SurfacesModelSession& session, int event_count) {
  LongJournalAddPerfResult result;
  result.seed = SeedLongJournalOnModel(session, event_count);
  result.journal_size_after_seed = result.seed.journal_event_count;
  WaitModelPublicationBacklogDrained(session);
  result.publish_count_after_drain = session.channel.publish_count();
  result.timing = RunMeasuredAddSurfaceOnFirstSurfaceImpl(session);
  RunOnModelPlain(session, [&](Application& app) {
    result.fingerprint =
        SurfaceJournalFingerprint::Capture(*app.surfaces->surfaces[0]);
  });
  return result;
}

void ReportLongJournalAddPerf(char const* scenario, char const* label,
                              LongJournalAddPerfResult const& result) {
  auto const& t = result.timing;
  auto const& s = result.seed;
  std::cout << "long_journal_add_perf scenario=" << scenario
            << " label=" << label << " journal_events=" << s.journal_event_count
            << " seed_publish_delta=" << (s.publish_count_after - s.publish_count_before)
            << " dirty_obj_ids~" << LongJournalAddPerfResult::kDirtyObjIdsSingleSurfaceSeed
            << " publish_after_drain=" << result.publish_count_after_drain
            << " channel_busy_after_seed=" << (s.channel_busy_after_seed ? 1 : 0)
            << " publish_at_post=" << t.publish_count_at_post
            << " channel_busy_at_post=" << (t.channel_busy_at_post ? 1 : 0)
            << " client_wait_ms=" << (t.client_wait_us / 1000)
            << " queue_wait_ms=" << (t.queue_wait_us / 1000)
            << " root_access_ms=" << (t.root_access_us / 1000)
            << " add_body_ms=" << (t.add_body_us / 1000)
            << " cleanup_ms=" << (t.cleanup_us / 1000) << '\n';
}

void AssertReleaseLongJournalAddImmediateGate(
    char const* label, LongJournalAddPerfResult const& result) {
#ifndef NDEBUG
  (void)label;
  (void)result;
#else
  if (result.timing.client_wait_us >= 4'000'000) {
    std::cerr << "PERF FAIL (immediate): " << label
              << " client_wait_ms=" << (result.timing.client_wait_us / 1000)
              << " (Release threshold 4000 ms)"
              << " queue_wait_ms=" << (result.timing.queue_wait_us / 1000)
              << " root_access_ms=" << (result.timing.root_access_us / 1000)
              << " add_body_ms=" << (result.timing.add_body_us / 1000)
              << " cleanup_ms=" << (result.timing.cleanup_us / 1000)
              << " seed_publish_delta="
              << (result.seed.publish_count_after - result.seed.publish_count_before)
              << '\n';
    LJ_CHECK(false);
  }
#endif
}

}  // namespace apptraverse::test
