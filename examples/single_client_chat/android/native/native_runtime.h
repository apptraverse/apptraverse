#ifndef APPTRAVERSE_EXAMPLES_ANDROID_NATIVE_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_NATIVE_RUNTIME_H_

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "aether/aether_app.h"
#include "aether/ae_context.h"
#include "aether/ptr/rc_ptr.h"

#include "apptraverse/app.h"

#include "android_chat_presenter.h"
#include "ui_bridge.h"

namespace apptraverse::android {

// Process-wide native core: one AetherApp, one Domain, one storage root.
class NativeRuntime {
 public:
  NativeRuntime(std::string state_dir, UiBridge ui_bridge);
  ~NativeRuntime();

  NativeRuntime(NativeRuntime const&) = delete;
  NativeRuntime& operator=(NativeRuntime const&) = delete;

  void Run();
  void Stop();

  // Thread-safe. Returns false when the text is empty after trim.
  bool QueueSend(std::string text);
  void RequestSnapshot();

 private:
  bool Setup();
  bool LoadOrBuildGraph();
  bool LoadPresenters();
  void Teardown();
  void DrainPendingSends();
  void WaitForUniqueTimestamp();
  void PublishStatus(std::string const& status);
  void PublishTranscript(std::string const& transcript);
  void PublishSnapshot();
  void LogJournalSize();
  void SaveState();
  void WakeUp();

  std::string state_dir_;
  UiBridge ui_bridge_;
  ae::RcPtr<ae::AetherApp> aether_app_;
  App::ptr app_;
  AndroidChatPresenter* chat_presenter_{nullptr};

  std::mutex pending_lock_;
  std::vector<std::string> pending_sends_;
  std::atomic<ae::TaskScheduler*> scheduler_{nullptr};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> snapshot_requested_{false};
};

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_NATIVE_RUNTIME_H_
