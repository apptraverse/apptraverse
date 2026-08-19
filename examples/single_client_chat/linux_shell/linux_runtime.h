#ifndef APPTRAVERSE_EXAMPLES_LINUX_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_LINUX_RUNTIME_H_

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aether/ae_context.h"
#include "aether/aether_app.h"
#include "aether/client.h"
#include "aether/obj/idomain_storage.h"
#include "aether/ptr/rc_ptr.h"

#include "aether_p2p_transport.h"
#include "chat_component.h"
#include "linux_chat_presenter.h"
#include "linux_window_presenter.h"
#include "model/app.h"

namespace apptraverse::linux_host {

struct UiSink {
  std::function<void(std::string)> post_local_uid;
  std::function<void(std::string)> post_transcript;
  std::function<void(std::string)> post_error;
};

// Background Aether / ChatComponent loop. GTK widgets stay on the GTK thread.
class LinuxRuntime {
 public:
  LinuxRuntime(std::string state_dir, UiSink ui);
  ~LinuxRuntime();

  LinuxRuntime(LinuxRuntime const&) = delete;
  LinuxRuntime& operator=(LinuxRuntime const&) = delete;

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
  UiSink ui_;
  ae::RcPtr<ae::AetherApp> aether_app_;
  ae::IDomainStorage* domain_storage_{nullptr};
  App::ptr app_;
  ae::Client::ptr aether_client_;
  std::unique_ptr<examples::AetherP2pTransport> p2p_transport_;
  std::unique_ptr<chat::ChatComponent> chat_component_;
  LinuxWindowPresenter* window_presenter_{nullptr};
  LinuxChatPresenter* chat_presenter_{nullptr};

  std::mutex pending_lock_;
  std::vector<std::string> pending_sends_;
  std::vector<std::string> pending_peers_;
  std::atomic<ae::TaskScheduler*> scheduler_{nullptr};
  std::atomic<bool> stop_requested_{false};
};

}  // namespace apptraverse::linux_host

#endif  // APPTRAVERSE_EXAMPLES_LINUX_RUNTIME_H_
