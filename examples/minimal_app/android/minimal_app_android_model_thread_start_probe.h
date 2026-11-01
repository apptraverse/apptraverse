#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_MODEL_THREAD_START_PROBE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_MODEL_THREAD_START_PROBE_H_

#include <string>

namespace apptraverse::example::minimal {

// Verifies StartModelThread unblocks on worker entry, not session lifetime.
std::string RunAndroidModelThreadStartSyncProbe();

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_MODEL_THREAD_START_PROBE_H_
