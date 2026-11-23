#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_MODEL_THREAD_START_PROBE_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_MODEL_THREAD_START_PROBE_H_

#include <string>
#include <filesystem>
#include <jni.h>

namespace apptraverse::example::minimal {

// UI-thread caller owns a genuine Java root and an isolated app-cache path.
// Verifies nonblocking start and teardown against model worker entry/exit.
std::string RunAndroidModelThreadStartSyncProbe(
    JNIEnv* env, jobject content_root, std::filesystem::path const& state_dir,
    jobject ui_bridge);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_MODEL_THREAD_START_PROBE_H_
