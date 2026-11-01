#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_CHECKPOINT_TEST_GATE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_CHECKPOINT_TEST_GATE_H_

namespace apptraverse::example::minimal {

void EnableAndroidCheckpointPersistTestGate();
void DisableAndroidCheckpointPersistTestGate();
void OpenAndroidCheckpointPersistTestGate();
void WaitAndroidCheckpointPersistTestGateIfEnabled();

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_CHECKPOINT_TEST_GATE_H_
