#include "minimal_app_android_checkpoint_test_gate.h"

#include <condition_variable>
#include <mutex>

namespace apptraverse::example::minimal {
namespace {

std::mutex gate_mu;
std::condition_variable gate_cv;
bool gate_enabled = false;
bool gate_open = true;

}  // namespace

void EnableAndroidCheckpointPersistTestGate() {
  std::lock_guard<std::mutex> lock{gate_mu};
  gate_enabled = true;
  gate_open = false;
}

void DisableAndroidCheckpointPersistTestGate() {
  std::lock_guard<std::mutex> lock{gate_mu};
  gate_enabled = false;
  gate_open = true;
  gate_cv.notify_all();
}

void OpenAndroidCheckpointPersistTestGate() {
  std::lock_guard<std::mutex> lock{gate_mu};
  gate_open = true;
  gate_cv.notify_all();
}

void WaitAndroidCheckpointPersistTestGateIfEnabled() {
  std::unique_lock<std::mutex> lock{gate_mu};
  gate_cv.wait(lock, [] { return !gate_enabled || gate_open; });
}

}  // namespace apptraverse::example::minimal
