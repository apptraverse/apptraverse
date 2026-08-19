#ifndef APPTRAVERSE_EXAMPLES_APPLE_CHAT_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_APPLE_CHAT_RUNTIME_H_

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aether/aether_app.h"
#include "aether/client.h"

#include "aether/obj/idomain_storage.h"

#include "model/app.h"

#include "../../common/aether_p2p_transport.h"
#include "../../common/chat_component.h"
#include "apple_chat_presenter.h"
#include "apple_window_presenter.h"

namespace apptraverse::apple {

class AppleChatRuntime {
 public:
  struct UiCallbacks {
    std::function<void(std::string)> on_aether_uid;
    std::function<void(std::string)> on_transcript;
  };

  AppleChatRuntime(std::string state_dir, std::string aether_client_name,
                   std::string local_client_name, UiCallbacks callbacks);
  ~AppleChatRuntime();

  AppleChatRuntime(AppleChatRuntime const&) = delete;
  AppleChatRuntime& operator=(AppleChatRuntime const&) = delete;

  void Run();
  void Stop();

  bool QueueSend(std::string text);
  bool QueueAddPeer(std::string uid);

 private:
  bool Setup();
  bool LoadOrBuildGraph();
  bool LoadPresenters();
  bool SelectAetherClient();
  void StartP2pTransport();
  bool StartChatSync();
  void Teardown();
  void DrainPendingSends();
  void DrainPendingPeers();
  void PublishTranscript(std::string const& transcript);
  void SaveState();
  void WakeUp();

  std::string state_dir_;
  std::string aether_client_name_;
  std::string local_client_name_;
  UiCallbacks callbacks_;
  std::unique_ptr<ae::AetherApp> aether_app_;
  ae::IDomainStorage* domain_storage_{nullptr};
  App::ptr app_;
  ae::Client::ptr aether_client_;
  std::unique_ptr<examples::AetherP2pTransport> p2p_transport_;
  std::unique_ptr<chat::ChatComponent> chat_component_;
  AppleWindowPresenter* window_presenter_{nullptr};
  AppleChatPresenter* chat_presenter_{nullptr};

  std::mutex pending_lock_;
  std::vector<std::string> pending_sends_;
  std::vector<std::string> pending_peers_;
  std::atomic<ae::TaskScheduler*> scheduler_{nullptr};
  std::atomic<bool> stop_requested_{false};
};

}  // namespace apptraverse::apple

#endif  // APPTRAVERSE_EXAMPLES_APPLE_CHAT_RUNTIME_H_
