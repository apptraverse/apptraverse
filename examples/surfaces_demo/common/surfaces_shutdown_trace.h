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
  // After QueueForegroundAsCurrent + QueueAllWindowBounds (model work not yet drained).
  static void MarkCloseUiStateQueued();
  static void MarkModelDrainedAcceptedTasks();
  static void MarkClearLiveNotifiersDone();
  static void MarkApplicationSaveStart();
  static void MarkApplicationSaveEnd();
  // Immediately before ~Application / ~Domain (after Save returns).
  static void MarkApplicationGraphTeardownStart();
  static void MarkModelDomainDestroyed();
  static void MarkDoneEventSet();
  static void MarkUnloadPresentersDone();
  static void MarkWindowsGone();
  static void MarkProcessExit();

  static void LogStorageStoreStats(std::uint64_t layer_store_calls,
                                   std::uint64_t layers_bytes_accepted,
                                   std::uint64_t layers_written,
                                   std::uint64_t layers_skipped_unchanged,
                                   std::uint64_t layers_written_bytes,
                                   std::uint64_t layers_failed_commits,
                                   std::uint64_t layers_compare_bytes_read,
                                   std::uint64_t layers_size_mismatch_writes,
                                   std::uint64_t layers_new_file_writes);

  static void LogSaveCallPhases(std::int64_t owning_ptr_ms,
                                std::int64_t save_root_ms);

  static void LogCheckpointCommitStats(bool success,
                                       std::uint64_t checkpoint_bytes);

  static void LogInstrumentedSaveStats(std::uint64_t store_calls,
                                       std::uint64_t store_bytes_accepted,
                                       std::uint64_t store_disk_bytes_written,
                                       std::uint64_t store_new_layers,
                                       std::uint64_t store_overwrite_layers,
                                       std::uint64_t store_skipped_unchanged_layers,
                                       std::uint64_t store_failed_commits);

  static void FlushSummary();

 private:
  static void Mark(char const* label);
  static Clock::time_point Origin();
  static Clock::time_point close_requested_;
  static Clock::time_point close_ui_state_queued_;
  static Clock::time_point clear_live_notifiers_done_;
  static Clock::time_point model_drained_;
  static Clock::time_point save_start_;
  static Clock::time_point save_end_;
  static Clock::time_point graph_teardown_start_;
  static Clock::time_point model_domain_destroyed_;
  static Clock::time_point done_event_;
  static Clock::time_point unload_done_;
  static Clock::time_point windows_gone_;
  static bool origin_set_;
  static bool close_requested_set_;
  static bool close_ui_state_queued_set_;
  static bool clear_live_notifiers_done_set_;
  static bool model_drained_set_;
  static bool save_start_set_;
  static bool save_end_set_;
  static bool graph_teardown_start_set_;
  static bool model_domain_destroyed_set_;
  static bool done_event_set_flag_;
  static bool unload_done_set_;
  static bool windows_gone_set_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_SHUTDOWN_TRACE_H_
