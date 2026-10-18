#ifndef APPTRAVERSE_SURFACES_TEST_PRESENTER_COUNTERS_H_
#define APPTRAVERSE_SURFACES_TEST_PRESENTER_COUNTERS_H_

#include <atomic>
#include <cstdint>
#include <vector>

namespace apptraverse::test {

struct TestSurfacePresenter {
  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_model_changed_calls{0};
  static inline std::atomic<int> on_unload_calls{0};
  static inline std::vector<std::uint32_t> loaded_ids;
  static inline std::vector<std::uint32_t> unloaded_ids;

  static void ResetCounts() {
    on_load_calls.store(0);
    on_model_changed_calls.store(0);
    on_unload_calls.store(0);
    loaded_ids.clear();
    unloaded_ids.clear();
  }
};

}  // namespace apptraverse::test

#endif
