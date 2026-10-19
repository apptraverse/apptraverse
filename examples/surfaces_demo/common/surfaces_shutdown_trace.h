#ifndef APPTRAVERSE_SURFACES_SHUTDOWN_TRACE_H_
#define APPTRAVERSE_SURFACES_SHUTDOWN_TRACE_H_

#include <chrono>
#include <cstdint>

namespace apptraverse {

// Opt-in via APPTRAVERSE_SURFACES_SHUTDOWN_TRACE=1 (stderr, steady_clock ms).
class SurfacesShutdownTrace {
 public:
  using Clock = std::chrono::steady_clock;

  static bool Enabled();

  static void MarkCloseRequested();
  static void MarkModelDrainedAcceptedTasks();
  static void MarkClearLiveNotifiersDone();
  static void MarkApplicationSaveStart();
  static void MarkApplicationSaveEnd();
  static void MarkModelDomainDestroyed();
  static void MarkDoneEventSet();
  static void MarkUnloadPresentersDone();
  static void MarkWindowsGone();
  static void MarkProcessExit();

  static void LogStorageStoreStats(std::uint64_t layer_store_calls,
                                   std::uint64_t layers_written,
                                   std::uint64_t layers_skipped_unchanged,
                                   std::uint64_t layers_written_bytes);

  static void LogInstrumentedSaveStats(std::uint64_t store_calls,
                                       std::uint64_t store_bytes,
                                       std::uint64_t store_new_layers,
                                       std::uint64_t store_overwrite_layers,
                                       std::uint64_t store_skipped_unchanged_layers);

  static void FlushSummary();

 private:
  static void Mark(char const* label);
  static Clock::time_point Origin();
  static Clock::time_point close_requested_;
  static Clock::time_point windows_gone_;
  static bool origin_set_;
  static bool close_requested_set_;
  static bool windows_gone_set_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_SHUTDOWN_TRACE_H_
