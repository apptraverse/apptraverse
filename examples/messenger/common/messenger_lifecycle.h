#ifndef APPTRAVERSE_MESSENGER_LIFECYCLE_H_
#define APPTRAVERSE_MESSENGER_LIFECYCLE_H_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node.h"
#include "apptraverse/publication_channel.h"

#include "chat_aether_runtime.h"

namespace apptraverse {

class Application;

enum class MessengerPublicationKind {
  Initial,
  Incremental,
};

// Model thread + publication + persistence + Aether runtime ownership.
struct MessengerModelSession {
  using ModelWork = ModelObjectProxy::ModelWork;

  std::filesystem::path state_dir;
  PublicationChannel<3> channel;
  std::mutex mu;
  std::condition_variable cv;
  bool stop{false};
  std::deque<ModelWork> pending_work;
  std::unique_ptr<example::chat_demo::ChatAetherRuntime> aether;

  void RequestStop();
  // Rejected after RequestStop. Work already queued is accepted and drained.
  void Post(ModelWork work);
  void Run(std::function<void(MessengerPublicationKind)> on_published);
};

void ApplyMessengerStructural(std::vector<std::uint8_t> const& bytes,
                              Application& ui_application,
                              ae::IDomainStorage& ui_storage, void* host,
                              ModelObjectProxy* model_proxy = nullptr);

}  // namespace apptraverse

#endif  // APPTRAVERSE_MESSENGER_LIFECYCLE_H_
