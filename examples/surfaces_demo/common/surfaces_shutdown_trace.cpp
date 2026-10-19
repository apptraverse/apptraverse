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

  Mark("close_ui_state_queued");

}



void SurfacesShutdownTrace::MarkModelDrainedAcceptedTasks() {

  model_drained_ = Clock::now();

  model_drained_set_ = true;

  Mark("model_drained_accepted_tasks");

}



void SurfacesShutdownTrace::MarkClearLiveNotifiersDone() {

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

    std::uint64_t layer_store_calls, std::uint64_t layers_written,

    std::uint64_t layers_skipped_unchanged, std::uint64_t layers_written_bytes) {

  if (!TraceEnabled()) {

    return;

  }

  std::cerr << "surfaces_shutdown_trace save_store_stats layer_store_calls="

            << layer_store_calls << " layers_written=" << layers_written

            << " layers_skipped_unchanged=" << layers_skipped_unchanged

            << " layers_written_bytes=" << layers_written_bytes << '\n';

}



void SurfacesShutdownTrace::LogInstrumentedSaveStats(

    std::uint64_t store_calls, std::uint64_t store_bytes,

    std::uint64_t store_new_layers, std::uint64_t store_overwrite_layers,

    std::uint64_t store_skipped_unchanged_layers) {

  if (!TraceEnabled()) {

    return;

  }

  std::cerr << "surfaces_shutdown_trace save_instrumented store_calls="

            << store_calls << " store_bytes=" << store_bytes

            << " store_new_layers=" << store_new_layers

            << " store_overwrite_layers=" << store_overwrite_layers

            << " store_skipped_unchanged_layers="

            << store_skipped_unchanged_layers << '\n';

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

  if (done_event_set_flag_ && unload_done_set_) {

    std::cerr << "surfaces_shutdown_trace summary unload_presenters_ms="

              << MsBetween(done_event_, unload_done_) << '\n';

  }

}



}  // namespace apptraverse

