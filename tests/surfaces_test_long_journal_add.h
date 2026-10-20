#ifndef APPTRAVERSE_TESTS_SURFACES_TEST_LONG_JOURNAL_ADD_H_
#define APPTRAVERSE_TESTS_SURFACES_TEST_LONG_JOURNAL_ADD_H_

#include <chrono>
#include <cstdint>
#include <vector>

#include "apptraverse/application/model_publication_session.h"
#include "apptraverse/surfaces/surfaces_model.h"

#include "surfaces_test_publication_ui.h"

namespace apptraverse {
using SurfacesModelSession = ModelPublicationSession;
}

namespace apptraverse::test {

struct SurfaceJournalFingerprint {
  std::size_t journal_size{0};
  std::vector<std::uint32_t> event_obj_ids;
  std::vector<std::uint32_t> event_class_ids;
  std::vector<std::int32_t> presentation_widths;
  std::vector<std::int32_t> presentation_heights;

  static SurfaceJournalFingerprint Capture(Surface const& surface);
  void ExpectEqual(SurfaceJournalFingerprint const& other) const;
};

struct LongJournalSeedStats {
  std::size_t journal_event_count{0};
  std::size_t dirty_obj_ids_after_seed{0};
  std::uint64_t publish_count_before{0};
  std::uint64_t publish_count_after{0};
  bool channel_busy_after_seed{false};
};

struct LongJournalPublicationDrainStats {
  std::size_t incremental_applied_to_ui{0};
  std::uint64_t publish_count_at_completion{0};
  std::size_t pending_dirty_at_completion{0};
};

struct LongJournalAddTiming {
  std::int64_t queue_wait_us{0};
  std::int64_t root_access_us{0};
  std::int64_t add_body_us{0};
  std::int64_t cleanup_us{0};
  std::int64_t client_wait_us{0};
  std::uint64_t publish_count_at_post{0};
  std::uint64_t publish_count_at_task_start{0};
  bool channel_busy_at_post{false};
};

struct LongJournalAddPerfResult {
  LongJournalAddTiming timing;
  LongJournalSeedStats seed;
  SurfaceJournalFingerprint fingerprint;
  LongJournalPublicationDrainStats drain;
  std::size_t journal_size_after_seed{0};
  std::uint64_t publish_count_after_drain{0};
};

LongJournalSeedStats SeedLongJournalOnModel(SurfacesModelSession& session,
                                            int event_count);

LongJournalPublicationDrainStats WaitModelPublicationBacklogDrained(
    SurfacesModelSession& session, SurfacesUiPublicationConsumer& ui,
    LongJournalSeedStats const& seed);

void VerifyLongJournalSeedJournalContent(Surface const& surface,
                                         int event_count);

void ResetIncrementalPrepBarrier(SurfacesModelSession& session);
void EnableIncrementalPrepBarrier(SurfacesModelSession& session);
bool WaitIncrementalPrepAtCheckpoint(SurfacesModelSession& session,
                                     std::chrono::milliseconds timeout);
void NotifyIncrementalPrepAddPosted(SurfacesModelSession& session);
void NotifyIncrementalPrepContinue(SurfacesModelSession& session);

LongJournalAddTiming RunMeasuredAddSurfaceOnFirstSurface(
    SurfacesModelSession& session);

LongJournalAddPerfResult RunAddAfterPublicationDrain(
    SurfacesModelSession& session, int event_count,
    SurfacesUiPublicationConsumer* ui_consumer = nullptr);

LongJournalAddPerfResult RunAddImmediatelyAfterChanges(
    SurfacesModelSession& session, int event_count,
    SurfacesUiPublicationConsumer* ui_consumer = nullptr);

struct LongJournalAddDuringPrepResult {
  LongJournalAddPerfResult add;
  std::int64_t add_post_to_future_ready_us{0};
  std::int64_t checkpoint_wait_for_add_post_us{0};
  std::int64_t prep_continue_to_add_start_us{0};
  std::uint64_t publish_count_at_prep{0};
  std::size_t pending_dirty_at_prep{0};
};

LongJournalAddDuringPrepResult RunAddDuringIncrementalPublicationPrep(
    SurfacesModelSession& session, int event_count);

void ReportLongJournalAddPerf(char const* scenario, char const* label,
                              LongJournalAddPerfResult const& result);

void AssertReleaseLongJournalAddImmediateGate(char const* label,
                                              LongJournalAddPerfResult const& result);

}  // namespace apptraverse::test

#endif  // APPTRAVERSE_TESTS_SURFACES_TEST_LONG_JOURNAL_ADD_H_
