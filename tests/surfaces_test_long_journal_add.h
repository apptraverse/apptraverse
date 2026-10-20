#ifndef APPTRAVERSE_TESTS_SURFACES_TEST_LONG_JOURNAL_ADD_H_
#define APPTRAVERSE_TESTS_SURFACES_TEST_LONG_JOURNAL_ADD_H_

#include <cstdint>
#include <vector>

#include "apptraverse/application/model_publication_session.h"
#include "apptraverse/surfaces/surfaces_model.h"

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
  std::uint64_t publish_count_before{0};
  std::uint64_t publish_count_after{0};
  bool channel_busy_after_seed{false};
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
  std::size_t journal_size_after_seed{0};
  std::uint64_t publish_count_after_drain{0};
  static constexpr int kDirtyObjIdsSingleSurfaceSeed{1};
};

LongJournalSeedStats SeedLongJournalOnModel(SurfacesModelSession& session,
                                            int event_count);

void WaitModelPublicationBacklogDrained(SurfacesModelSession& session);

LongJournalAddTiming RunMeasuredAddSurfaceOnFirstSurface(
    SurfacesModelSession& session);

LongJournalAddPerfResult RunAddAfterPublicationDrain(SurfacesModelSession& session,
                                                       int event_count);

LongJournalAddPerfResult RunAddImmediatelyAfterChanges(
    SurfacesModelSession& session, int event_count);

void ReportLongJournalAddPerf(char const* scenario, char const* label,
                              LongJournalAddPerfResult const& result);

void AssertReleaseLongJournalAddImmediateGate(char const* label,
                                              LongJournalAddPerfResult const& result);

}  // namespace apptraverse::test

#endif  // APPTRAVERSE_TESTS_SURFACES_TEST_LONG_JOURNAL_ADD_H_
