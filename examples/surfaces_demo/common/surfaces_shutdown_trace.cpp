#include "surfaces_shutdown_trace.h"



#include <cstdlib>

#include <iostream>



namespace apptraverse {

namespace {



bool TraceEnabled() {

  char const* v = std::getenv("APPTRAVERSE_SURFACES_SHUTDOWN_TRACE");

  return v != nullptr && v[0] == '1' && v[1] == '\0';

}



std::int64_t MsSince(SurfacesShutdownTrace::Clock::time_point origin,

                     SurfacesShutdownTrace::Clock::time_point t) {

  return std::chrono::duration_cast<std::chrono::milliseconds>(t - origin)

      .count();

}



std::int64_t MsBetween(SurfacesShutdownTrace::Clock::time_point a,

                       SurfacesShutdownTrace::Clock::time_point b) {

  return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();

}



}  // namespace



SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::close_requested_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::close_ui_state_queued_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::clear_live_notifiers_done_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::model_drained_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::save_start_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::save_end_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::graph_teardown_start_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::model_domain_destroyed_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::done_event_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::unload_done_{};

SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::windows_gone_{};

bool SurfacesShutdownTrace::origin_set_{false};

bool SurfacesShutdownTrace::close_requested_set_{false};

bool SurfacesShutdownTrace::close_ui_state_queued_set_{false};

bool SurfacesShutdownTrace::clear_live_notifiers_done_set_{false};

bool SurfacesShutdownTrace::model_drained_set_{false};

bool SurfacesShutdownTrace::save_start_set_{false};

bool SurfacesShutdownTrace::save_end_set_{false};

bool SurfacesShutdownTrace::graph_teardown_start_set_{false};

bool SurfacesShutdownTrace::model_domain_destroyed_set_{false};

bool SurfacesShutdownTrace::done_event_set_flag_{false};

bool SurfacesShutdownTrace::unload_done_set_{false};

bool SurfacesShutdownTrace::windows_gone_set_{false};



bool SurfacesShutdownTrace::Enabled() { return TraceEnabled(); }



SurfacesShutdownTrace::Clock::time_point SurfacesShutdownTrace::Origin() {

  static Clock::time_point origin = Clock::now();

  return origin;

}



void SurfacesShutdownTrace::Mark(char const* label) {

  if (!TraceEnabled()) {

    return;

  }

  auto const now = Clock::now();

  if (!origin_set_) {

    origin_set_ = true;

  }

  std::cerr << "surfaces_shutdown_trace " << label << " t_ms="

            << MsSince(Origin(), now) << '\n';

}



void SurfacesShutdownTrace::MarkCloseRequested() {

  close_requested_ = Clock::now();

  close_requested_set_ = true;

  Mark("close_request");

}



void SurfacesShutdownTrace::MarkCloseUiStateQueued() {

  close_ui_state_queued_ = Clock::now();

  close_ui_state_queued_set_ = true;

  Mark("close_ui_state_queued");

}



void SurfacesShutdownTrace::MarkModelDrainedAcceptedTasks() {

  model_drained_ = Clock::now();

  model_drained_set_ = true;

  Mark("model_drained_accepted_tasks");

}



void SurfacesShutdownTrace::MarkClearLiveNotifiersDone() {

  clear_live_notifiers_done_ = Clock::now();

  clear_live_notifiers_done_set_ = true;

  Mark("clear_live_notifiers_done");

}



void SurfacesShutdownTrace::MarkApplicationSaveStart() {

  save_start_ = Clock::now();

  save_start_set_ = true;

  Mark("application_save_start");

}



void SurfacesShutdownTrace::MarkApplicationSaveEnd() {

  save_end_ = Clock::now();

  save_end_set_ = true;

  Mark("application_save_end");

}



void SurfacesShutdownTrace::MarkApplicationGraphTeardownStart() {

  graph_teardown_start_ = Clock::now();

  graph_teardown_start_set_ = true;

  Mark("application_graph_teardown_start");

}



void SurfacesShutdownTrace::MarkModelDomainDestroyed() {

  model_domain_destroyed_ = Clock::now();

  model_domain_destroyed_set_ = true;

  Mark("model_domain_destroyed");

}



void SurfacesShutdownTrace::MarkDoneEventSet() {

  done_event_ = Clock::now();

  done_event_set_flag_ = true;

  Mark("done_event_set");

}



void SurfacesShutdownTrace::MarkUnloadPresentersDone() {

  unload_done_ = Clock::now();

  unload_done_set_ = true;

  Mark("unload_presenters_done");

}



void SurfacesShutdownTrace::MarkWindowsGone() {

  windows_gone_ = Clock::now();

  windows_gone_set_ = true;

  Mark("windows_gone");

}



void SurfacesShutdownTrace::MarkProcessExit() { Mark("process_exit"); }



void SurfacesShutdownTrace::LogStorageStoreStats(

    std::uint64_t layer_store_calls, std::uint64_t layers_bytes_accepted,

    std::uint64_t layers_written, std::uint64_t layers_skipped_unchanged,

    std::uint64_t layers_written_bytes, std::uint64_t layers_failed_commits,
    std::uint64_t layers_compare_bytes_read,
    std::uint64_t layers_size_mismatch_writes,
    std::uint64_t layers_new_file_writes) {

  if (!TraceEnabled()) {

    return;

  }

  std::cerr << "surfaces_shutdown_trace save_store_stats layer_store_calls="

            << layer_store_calls << " layers_bytes_accepted="

            << layers_bytes_accepted << " layers_written=" << layers_written

            << " layers_skipped_unchanged=" << layers_skipped_unchanged

            << " layers_written_bytes=" << layers_written_bytes

            << " layers_failed_commits=" << layers_failed_commits

            << " layers_compare_bytes_read=" << layers_compare_bytes_read

            << " layers_size_mismatch_writes=" << layers_size_mismatch_writes

            << " layers_new_file_writes=" << layers_new_file_writes << '\n';

}



void SurfacesShutdownTrace::LogSaveCallPhases(std::int64_t owning_ptr_ms,

                                              std::int64_t save_root_ms) {

  if (!TraceEnabled()) {

    return;

  }

  std::cerr << "surfaces_shutdown_trace save_call_phases owning_ptr_ms="

            << owning_ptr_ms << " save_root_ms=" << save_root_ms << '\n';

}

void SurfacesShutdownTrace::LogCheckpointCommitStats(bool success,
                                                     std::uint64_t checkpoint_bytes) {
  if (!TraceEnabled()) {
    return;
  }
  std::cerr << "surfaces_shutdown_trace checkpoint_commit success="
            << (success ? 1 : 0)
            << " checkpoint_bytes=" << checkpoint_bytes << '\n';
}



void SurfacesShutdownTrace::LogInstrumentedSaveStats(

    std::uint64_t store_calls, std::uint64_t store_bytes_accepted,

    std::uint64_t store_disk_bytes_written, std::uint64_t store_new_layers,

    std::uint64_t store_overwrite_layers,

    std::uint64_t store_skipped_unchanged_layers,

    std::uint64_t store_failed_commits) {

  if (!TraceEnabled()) {

    return;

  }

  std::cerr << "surfaces_shutdown_trace save_instrumented store_calls="

            << store_calls << " store_bytes_accepted=" << store_bytes_accepted

            << " store_disk_bytes_written=" << store_disk_bytes_written

            << " store_new_layers=" << store_new_layers

            << " store_overwrite_layers=" << store_overwrite_layers

            << " store_skipped_unchanged_layers="

            << store_skipped_unchanged_layers << " store_failed_commits="

            << store_failed_commits << '\n';

}



void SurfacesShutdownTrace::FlushSummary() {

  if (!TraceEnabled() || !close_requested_set_) {

    return;

  }

  auto const now = Clock::now();

  std::cerr << "surfaces_shutdown_trace summary close_to_exit_ms="

            << MsSince(close_requested_, now) << '\n';

  if (windows_gone_set_) {

    std::cerr << "surfaces_shutdown_trace summary close_to_windows_gone_ms="

              << MsSince(close_requested_, windows_gone_) << '\n';

  }

  if (close_requested_set_ && close_ui_state_queued_set_) {

    std::cerr << "surfaces_shutdown_trace summary close_to_ui_state_queued_ms="

              << MsBetween(close_requested_, close_ui_state_queued_) << '\n';

  }

  if (close_ui_state_queued_set_ && model_drained_set_) {

    std::cerr << "surfaces_shutdown_trace summary ui_state_queued_to_model_drained_ms="

              << MsBetween(close_ui_state_queued_, model_drained_) << '\n';

  }

  if (model_drained_set_ && clear_live_notifiers_done_set_) {

    std::cerr << "surfaces_shutdown_trace summary model_drained_to_clear_notifiers_ms="

              << MsBetween(model_drained_, clear_live_notifiers_done_) << '\n';

  }

  if (clear_live_notifiers_done_set_ && save_start_set_) {

    std::cerr << "surfaces_shutdown_trace summary clear_notifiers_to_save_start_ms="

              << MsBetween(clear_live_notifiers_done_, save_start_) << '\n';

  }

  if (close_requested_set_ && model_drained_set_) {

    std::cerr << "surfaces_shutdown_trace summary close_to_model_drained_ms="

              << MsBetween(close_requested_, model_drained_) << '\n';

  }

  if (save_start_set_ && save_end_set_) {

    std::cerr << "surfaces_shutdown_trace summary application_save_ms="

              << MsBetween(save_start_, save_end_) << '\n';

  }

  if (graph_teardown_start_set_ && model_domain_destroyed_set_) {

    std::cerr << "surfaces_shutdown_trace summary application_graph_teardown_ms="

              << MsBetween(graph_teardown_start_, model_domain_destroyed_) << '\n';

  }

  if (model_domain_destroyed_set_ && done_event_set_flag_) {

    std::cerr << "surfaces_shutdown_trace summary model_destroyed_to_done_event_ms="

              << MsBetween(model_domain_destroyed_, done_event_) << '\n';

  }

  if (done_event_set_flag_ && unload_done_set_) {

    std::cerr << "surfaces_shutdown_trace summary done_event_to_unload_presenters_ms="

              << MsBetween(done_event_, unload_done_) << '\n';

  }

  if (unload_done_set_ && windows_gone_set_) {

    std::cerr << "surfaces_shutdown_trace summary unload_presenters_to_windows_gone_ms="

              << MsBetween(unload_done_, windows_gone_) << '\n';

  }

  if (windows_gone_set_) {

    std::cerr << "surfaces_shutdown_trace summary windows_gone_to_process_exit_ms="

              << MsBetween(windows_gone_, now) << '\n';

  }

}



}  // namespace apptraverse

