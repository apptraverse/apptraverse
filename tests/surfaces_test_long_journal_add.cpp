#include "surfaces_test_long_journal_add.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
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
    stats.dirty_obj_ids_after_seed = session.pending_dirty.ordered.size();
  });
  stats.publish_count_after = session.channel.publish_count();
  stats.channel_busy_after_seed = session.channel.is_publication_busy();
  return stats;
}

void ResetIncrementalPrepBarrier(SurfacesModelSession& session) {
  std::lock_guard<std::mutex> lock{session.incremental_prep_barrier.mu};
  session.incremental_prep_barrier.enabled = false;
  session.incremental_prep_barrier.at_checkpoint = false;
  session.incremental_prep_barrier.add_posted = false;
  session.incremental_prep_barrier.continue_prep = false;
}

void EnableIncrementalPrepBarrier(SurfacesModelSession& session) {
  ResetIncrementalPrepBarrier(session);
  session.incremental_prep_barrier.enabled = true;
}

bool WaitIncrementalPrepAtCheckpoint(SurfacesModelSession& session,
                                     std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock{session.incremental_prep_barrier.mu};
  return session.incremental_prep_barrier.cv.wait_for(lock, timeout, [&] {
    return session.incremental_prep_barrier.at_checkpoint;
  });
}

void NotifyIncrementalPrepAddPosted(SurfacesModelSession& session) {
  std::lock_guard<std::mutex> lock{session.incremental_prep_barrier.mu};
  session.incremental_prep_barrier.add_posted = true;
  session.incremental_prep_barrier.cv.notify_all();
}

void NotifyIncrementalPrepContinue(SurfacesModelSession& session) {
  std::lock_guard<std::mutex> lock{session.incremental_prep_barrier.mu};
  session.incremental_prep_barrier.continue_prep = true;
  session.incremental_prep_barrier.cv.notify_all();
}

void VerifyLongJournalSeedJournalContent(Surface const& surface,
                                         int event_count) {
  LJ_CHECK(surface.journal.size() >= static_cast<std::size_t>(event_count));
  int matched = 0;
  for (EventRecord const& record : surface.journal) {
    if (SurfacePresentationSizeChangedEvent::ptr size_event{record.event}) {
      LJ_CHECK(size_event->width == 500 + matched);
      LJ_CHECK(size_event->height == 700 - matched);
      ++matched;
    }
  }
  LJ_CHECK(matched == event_count);
}

LongJournalPublicationDrainStats WaitModelPublicationBacklogDrained(
    SurfacesModelSession& session, SurfacesUiPublicationConsumer& ui,
    LongJournalSeedStats const& seed) {
  LongJournalPublicationDrainStats stats;
  std::size_t const expected_ui_applies = seed.dirty_obj_ids_after_seed;
  std::uint64_t const publish_baseline = seed.publish_count_after;

  auto apply_unread = [&] {
    std::vector<std::uint8_t> bytes;
    {
      std::lock_guard<std::mutex> lock{session.mu};
      if (!session.channel.has_unread_published()) {
        return false;
      }
      bytes = session.channel.TakePublishedCopy();
    }
    session.cv.notify_all();
    LJ_CHECK(!bytes.empty());
    ui.ApplyStructuralBytes(bytes);
    return true;
  };

#ifdef NDEBUG
  auto const deadline = Clock::now() + kModelWatchdog;
#else
  auto const deadline = Clock::now() + std::chrono::seconds{600};
#endif
  while (Clock::now() < deadline) {
    while (apply_unread()) {
    }

    stats.pending_dirty_at_completion =
        session.pending_dirty_count_observed.load();
    stats.publish_count_at_completion = session.channel.publish_count();
    stats.incremental_applied_to_ui = ui.incremental_applies;

    bool idle = false;
    {
      std::lock_guard<std::mutex> lock{session.mu};
      idle = session.pending_work.empty() &&
             !session.channel.has_unread_published() &&
             !session.channel.is_publication_busy() &&
             session.pending_dirty_count_observed.load() == 0 &&
             !session.incremental_publication_in_progress.load();
    }
    if (idle && ui.incremental_applies >= expected_ui_applies &&
        session.channel.publish_count() >= publish_baseline + expected_ui_applies) {
      break;
    }

    std::unique_lock<std::mutex> lock{session.mu};
    session.cv.wait_for(lock, std::chrono::milliseconds{20}, [&] {
      return session.channel.has_unread_published() ||
             session.pending_dirty_count_observed.load() > 0 ||
             !session.pending_work.empty() ||
             session.incremental_publication_in_progress.load();
    });
  }

  if (Clock::now() >= deadline) {
    std::cerr << "DRAIN TIMEOUT expected_ui_applies=" << expected_ui_applies
              << " ui_applied=" << ui.incremental_applies
              << " pending_dirty="
              << session.pending_dirty_count_observed.load()
              << " publish_count=" << session.channel.publish_count()
              << " baseline=" << publish_baseline
              << " in_progress="
              << session.incremental_publication_in_progress.load() << '\n';
    LJ_CHECK(false);
  }

  LJ_CHECK(session.pending_dirty_count_observed.load() == 0);
  LJ_CHECK(!session.incremental_publication_in_progress.load());
  LJ_CHECK(ui.incremental_applies >= expected_ui_applies);
  LJ_CHECK(session.channel.publish_count() >= publish_baseline + expected_ui_applies);
  return stats;
}

LongJournalAddTiming RunMeasuredAddSurfaceOnFirstSurface(
    SurfacesModelSession& session) {
  return RunMeasuredAddSurfaceOnFirstSurfaceImpl(session);
}

LongJournalAddPerfResult RunAddImmediatelyAfterChanges(
    SurfacesModelSession& session, int event_count,
    SurfacesUiPublicationConsumer* ui_consumer) {
  (void)ui_consumer;
  LongJournalAddPerfResult result;
  result.seed = SeedLongJournalOnModel(session, event_count);
  result.journal_size_after_seed = result.seed.journal_event_count;
  result.timing = RunMeasuredAddSurfaceOnFirstSurfaceImpl(session);
  RunOnModelPlain(session, [&](Application& app) {
    result.fingerprint =
        SurfaceJournalFingerprint::Capture(*app.surfaces->surfaces[0]);
    VerifyLongJournalSeedJournalContent(*app.surfaces->surfaces[0], event_count);
  });
  return result;
}

LongJournalAddPerfResult RunAddAfterPublicationDrain(
    SurfacesModelSession& session, int event_count,
    SurfacesUiPublicationConsumer* ui_consumer) {
  SurfacesUiPublicationConsumer local_ui;
  SurfacesUiPublicationConsumer& ui =
      ui_consumer != nullptr ? *ui_consumer : local_ui;
  if (ui_consumer == nullptr) {
    ui.BindSession(session);
    ui.LoadInitialFromSession(session);
  }

  LongJournalAddPerfResult result;
  result.seed = SeedLongJournalOnModel(session, event_count);
  result.journal_size_after_seed = result.seed.journal_event_count;
  result.drain = WaitModelPublicationBacklogDrained(session, ui, result.seed);
  result.publish_count_after_drain = result.drain.publish_count_at_completion;
  result.timing = RunMeasuredAddSurfaceOnFirstSurfaceImpl(session);
  RunOnModelPlain(session, [&](Application& app) {
    result.fingerprint =
        SurfaceJournalFingerprint::Capture(*app.surfaces->surfaces[0]);
    VerifyLongJournalSeedJournalContent(*app.surfaces->surfaces[0], event_count);
  });
  return result;
}

LongJournalAddDuringPrepResult RunAddDuringIncrementalPublicationPrep(
    SurfacesModelSession& session, int event_count) {
  LongJournalAddDuringPrepResult result;
  EnableIncrementalPrepBarrier(session);

  result.add.seed = SeedLongJournalOnModel(session, event_count);
  result.add.journal_size_after_seed = result.add.seed.journal_event_count;

  auto const checkpoint_wait_start = Clock::now();
  LJ_CHECK(WaitIncrementalPrepAtCheckpoint(session, kModelWatchdog));
  result.checkpoint_wait_for_add_post_us =
      Us(Clock::now() - checkpoint_wait_start);
  result.publish_count_at_prep = session.channel.publish_count();
  result.pending_dirty_at_prep = session.pending_dirty_count_observed.load();

  LongJournalAddTiming& t = result.add.timing;
  t.publish_count_at_post = session.channel.publish_count();
  t.channel_busy_at_post = session.channel.is_publication_busy();
  auto const add_post_start = Clock::now();
  std::promise<void> add_done;
  auto add_future = add_done.get_future();
  session.Post([&session, &t, &add_done, add_post_start](ae::Domain& domain) {
    auto const lambda_start = Clock::now();
    t.queue_wait_us = Us(lambda_start - add_post_start);
    t.publish_count_at_task_start = session.channel.publish_count();
    auto const root_start = Clock::now();
    Application& app = ModelApplication(domain);
    t.root_access_us = Us(Clock::now() - root_start);
    auto const add_start = Clock::now();
    app.surfaces->surfaces[0]->AddSurface();
    LJ_CHECK(app.surfaces->surfaces.size() == 2);
    auto const add_end = Clock::now();
    t.add_body_us = Us(add_end - add_start);
    auto const task_end = Clock::now();
    t.cleanup_us = Us(task_end - add_end);
    add_done.set_value();
  });
  NotifyIncrementalPrepAddPosted(session);
  auto const continue_start = Clock::now();
  NotifyIncrementalPrepContinue(session);
  result.prep_continue_to_add_start_us = Us(Clock::now() - continue_start);
  LJ_CHECK(add_future.wait_for(kModelWatchdog) == std::future_status::ready);
  t.client_wait_us = Us(Clock::now() - add_post_start);
  result.add_post_to_future_ready_us = t.client_wait_us;

  ResetIncrementalPrepBarrier(session);

  RunOnModelPlain(session, [&](Application& app) {
    result.add.fingerprint =
        SurfaceJournalFingerprint::Capture(*app.surfaces->surfaces[0]);
    VerifyLongJournalSeedJournalContent(*app.surfaces->surfaces[0], event_count);
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
            << " dirty_obj_ids=" << s.dirty_obj_ids_after_seed
            << " ui_applied=" << result.drain.incremental_applied_to_ui
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
