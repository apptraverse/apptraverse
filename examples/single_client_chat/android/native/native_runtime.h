#ifndef APPTRAVERSE_EXAMPLES_ANDROID_NATIVE_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_NATIVE_RUNTIME_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "aether/aether_app.h"
#include "aether/ae_context.h"
#include "aether/client.h"

#include "aether/obj/idomain_storage.h"

#include "model/app.h"

#include "../../common/aether_p2p_transport.h"
#include "../../common/chat_component.h"
#include "android_chat_presenter.h"
#include "android_window_presenter.h"
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
  // Thread-safe. Returns false when the UID is empty after trim.
  bool QueueAddPeer(std::string uid);
  void QueueWindowChanged(std::int32_t width, std::int32_t height,
                           std::int32_t density_dpi);

 private:
  struct PendingViewport {
    std::int32_t width{0};
    std::int32_t height{0};
    std::int32_t density_dpi{0};
  };

  bool Setup();
  bool LoadOrBuildGraph();
  bool LoadPresenters();
  bool SelectAetherClient();
  void StartP2pTransport();
  bool StartChatSync();
  void Teardown();
  void DrainPendingSends();
  void DrainPendingPeers();
  void DrainPendingViewports();
  void PublishTranscript(std::string const& transcript);
  void LogJournalSizes();
  void LogAppClientReady();
  void SaveState();
  void WakeUp();

  std::string state_dir_;
  UiBridge ui_bridge_;
  std::unique_ptr<ae::AetherApp> aether_app_;
  ae::IDomainStorage* domain_storage_{nullptr};
  App::ptr app_;
  ae::Client::ptr aether_client_;
  std::unique_ptr<examples::AetherP2pTransport> p2p_transport_;
  std::unique_ptr<chat::ChatComponent> chat_component_;
  AndroidWindowPresenter* window_presenter_{nullptr};
  AndroidChatPresenter* chat_presenter_{nullptr};

  std::mutex pending_lock_;
  std::vector<std::string> pending_sends_;
  std::vector<std::string> pending_peers_;
  std::vector<PendingViewport> pending_viewports_;
  std::set<std::string> visible_message_keys_;
  std::atomic<ae::TaskScheduler*> scheduler_{nullptr};
  std::atomic<bool> stop_requested_{false};
};

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_NATIVE_RUNTIME_H_
