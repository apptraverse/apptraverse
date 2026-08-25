#ifndef APPTRAVERSE_MODEL_EXECUTOR_H_
#define APPTRAVERSE_MODEL_EXECUTOR_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include "demo_ids.h"
#include "demo_model.h"
#include "immutable_object_store.h"
#include "ui_publication.h"

namespace apptraverse {

struct WindowBoundsCommand {
  std::uint32_t window_id{0};
  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t right{0};
  std::int32_t bottom{0};
  std::int32_t dpi{96};
  std::int32_t client_width{0};
  std::int32_t client_height{0};
};

struct AddMessageCommand {
  std::uint32_t chat_id{0};
  std::string text;
};

using ModelCommand = std::variant<WindowBoundsCommand, AddMessageCommand>;

class ModelExecutor {
 public:
  using PublishNotify =
      std::function<void(std::uint32_t root_id, PublicationChannel<3>*)>;

  ModelExecutor(Application& app, ImmutableObjectStore& store,
                PublishNotify notify);

  void Start();
  void RequestStop();
  void Join();
  void PostCommand(ModelCommand command);

  // Headless / tests: run one command+update+publish cycle on this thread.
  void PumpOnce(std::chrono::steady_clock::time_point now);

  void PublishAllRoots();
  bool SaveShutdown();  // runtime-save-ok: shutdown

  Window& window_a() { return *app_.window_a; }
  Window& window_b() { return *app_.window_b; }
  Application& app() { return app_; }

  PublicationChannel<3>& channel_a() { return channel_a_; }
  PublicationChannel<3>& channel_b() { return channel_b_; }

  std::uint64_t publication_count(std::uint32_t root_id) const;

 private:
  void ThreadMain();
  void DrainCommands();
  void Handle(WindowBoundsCommand const& command);
  void Handle(AddMessageCommand const& command);
  void UpdateAll(std::chrono::steady_clock::time_point now);
  void PublishRoot(Window& root, PublicationChannel<3>& channel);
  bool RootNeedsPublish(Window& root) const;

  Application& app_;
  ImmutableObjectStore& store_;
  PublishNotify notify_;
  PublicationChannel<3> channel_a_;
  PublicationChannel<3> channel_b_;
  std::unordered_map<std::uint32_t, std::uint64_t> last_pub_a_;
  std::unordered_map<std::uint32_t, std::uint64_t> last_pub_b_;
  std::vector<Node*> update_targets_;

  std::mutex mu_;
  std::condition_variable cv_;
  std::queue<ModelCommand> commands_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> accept_commands_{true};
  std::thread thread_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MODEL_EXECUTOR_H_
