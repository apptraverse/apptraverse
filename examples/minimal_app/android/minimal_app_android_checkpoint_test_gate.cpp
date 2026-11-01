#include "minimal_app_android_checkpoint_test_gate.h"

#include <condition_variable>
#include <mutex>

namespace apptraverse::example::minimal {
namespace {

std::mutex gate_mu;
std::condition_variable gate_cv;
bool gate_enabled = false;
bool gate_open = true;
int persist_gate_entry_count = 0;

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
  persist_gate_entry_count = 0;
  gate_cv.notify_all();
}

void OpenAndroidCheckpointPersistTestGate() {
  std::lock_guard<std::mutex> lock{gate_mu};
  gate_open = true;
  gate_cv.notify_all();
}

void WaitAndroidCheckpointPersistTestGateIfEnabled() {
  std::unique_lock<std::mutex> lock{gate_mu};
  if (gate_enabled) {
    ++persist_gate_entry_count;
    gate_cv.notify_all();
  }
  gate_cv.wait(lock, [] { return !gate_enabled || gate_open; });
}

int GetAndroidCheckpointPersistGateEntryCount() {
  std::lock_guard<std::mutex> lock{gate_mu};
  return persist_gate_entry_count;
}

bool WaitAndroidCheckpointPersistGateEntriesAtLeast(
    int expected, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock{gate_mu};
  return gate_cv.wait_for(lock, timeout, [&] {
    return persist_gate_entry_count >= expected;
  });
}

}  // namespace apptraverse::example::minimal
