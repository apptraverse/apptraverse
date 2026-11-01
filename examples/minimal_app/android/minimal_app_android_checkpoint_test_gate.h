#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_CHECKPOINT_TEST_GATE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_CHECKPOINT_TEST_GATE_H_

#include <chrono>

namespace apptraverse::example::minimal {

void SetAndroidCheckpointPersistTestGateRoot(char const* files_dir);

void EnableAndroidCheckpointPersistTestGate();
void DisableAndroidCheckpointPersistTestGate();
void OpenAndroidCheckpointPersistTestGate();
void WaitAndroidCheckpointPersistTestGateIfEnabled();

int GetAndroidCheckpointPersistGateEntryCount();

bool WaitAndroidCheckpointPersistGateEntriesAtLeast(
    int expected, std::chrono::milliseconds timeout);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_CHECKPOINT_TEST_GATE_H_
