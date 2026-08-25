#include "model_executor.h"

#include <cassert>

#include "demo_events.h"
#include "demo_log.h"

namespace apptraverse {
namespace {

Window* FindWindow(Application& app, std::uint32_t id) {
  if (app.window_a && app.window_a.id().id() == id) {
    app.window_a.Load();
    return &*app.window_a;
  }
  if (app.window_b && app.window_b.id().id() == id) {
    app.window_b.Load();
    return &*app.window_b;
  }
  return nullptr;
}

}  // namespace

ModelExecutor::ModelExecutor(Application& app, ImmutableObjectStore& store,
                             PublishNotify notify)
    : app_{app}, store_{store}, notify_{std::move(notify)} {
  assert(app_.window_a);
  assert(app_.window_b);
  app_.window_a.Load();
  app_.window_b.Load();
  update_targets_.push_back(&*app_.window_a);
  update_targets_.push_back(&*app_.window_b);
  if (app_.window_b->text_toolbar) {
    app_.window_b->text_toolbar.Load();
    update_targets_.push_back(&*app_.window_b->text_toolbar);
  }
  if (app_.window_b->color_toolbar) {
    app_.window_b->color_toolbar.Load();
    update_targets_.push_back(&*app_.window_b->color_toolbar);
  }
  if (app_.window_b->chat) {
    app_.window_b->chat.Load();
    update_targets_.push_back(&*app_.window_b->chat);
  }
}

void ModelExecutor::Start() {
  stop_ = false;
  accept_commands_ = true;
  thread_ = std::thread([this] { ThreadMain(); });
}

void ModelExecutor::RequestStop() {
  accept_commands_ = false;
  stop_ = true;
  cv_.notify_all();
}

void ModelExecutor::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ModelExecutor::PostCommand(ModelCommand command) {
  if (!accept_commands_.load()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock{mu_};
    commands_.push(std::move(command));
  }
  cv_.notify_one();
}

void ModelExecutor::ThreadMain() {
  auto next_update =
      std::chrono::steady_clock::now() + demo::kModelUpdatePeriod;
  while (!stop_.load()) {
    DrainCommands();
    auto const now = std::chrono::steady_clock::now();
    if (now >= next_update) {
      UpdateAll(now);
      next_update = now + demo::kModelUpdatePeriod;
    }
    PublishAllRoots();
    std::unique_lock<std::mutex> lock{mu_};
    cv_.wait_until(lock, next_update, [this] {
      return stop_.load() || !commands_.empty();
    });
  }
  DrainCommands();
  UpdateAll(std::chrono::steady_clock::now());
  PublishAllRoots();
}

void ModelExecutor::PumpOnce(std::chrono::steady_clock::time_point now) {
  DrainCommands();
  UpdateAll(now);
  PublishAllRoots();
}

void ModelExecutor::DrainCommands() {
  for (;;) {
    ModelCommand command;
    {
      std::lock_guard<std::mutex> lock{mu_};
      if (commands_.empty()) {
        return;
      }
      command = std::move(commands_.front());
      commands_.pop();
    }
    std::visit([this](auto const& value) { Handle(value); }, command);
  }
}

void ModelExecutor::Handle(WindowBoundsCommand const& command) {
  auto* window = FindWindow(app_, command.window_id);
  assert(window != nullptr);
  auto event =
      WindowBoundsChangedEvent::ptr::Create(ae::CreateWith{*window->domain});
  event->left = command.left;
  event->top = command.top;
  event->right = command.right;
  event->bottom = command.bottom;
  event->dpi = command.dpi;
  event->client_width = command.client_width;
  event->client_height = command.client_height;
  window->Commit(event);
  demo::DemoLog("commit WindowBounds window=" +
                std::to_string(command.window_id) +
                " gen=" + std::to_string(window->Generation()));
}

void ModelExecutor::Handle(AddMessageCommand const& command) {
  assert(app_.window_b);
  app_.window_b.Load();
  assert(app_.window_b->chat);
  app_.window_b->chat.Load();
  auto& chat = *app_.window_b->chat;
  assert(chat.obj_id.id() == command.chat_id);
  auto event = AddMessageEvent::ptr::Create(ae::CreateWith{*chat.domain});
  event->text = command.text;
  chat.Commit(event);
  demo::DemoLog("commit AddMessage chat=" + std::to_string(command.chat_id) +
                " gen=" + std::to_string(chat.Generation()) +
                " n=" + std::to_string(chat.messages.size()));
}

void ModelExecutor::UpdateAll(std::chrono::steady_clock::time_point now) {
  for (auto* node : update_targets_) {
    node->EnsureCurrentGeneration();
    node->Update(now);
  }
}

bool ModelExecutor::RootNeedsPublish(Window& root) const {
  auto const& last =
      root.obj_id.id() == app_.window_a.id().id() ? last_pub_a_ : last_pub_b_;
  auto needs = [&](Node& node) {
    auto it = last.find(node.obj_id.id());
    return it == last.end() || it->second != node.Generation();
  };
  if (needs(root)) {
    return true;
  }
  if (root.text_toolbar && needs(*root.text_toolbar)) {
    return true;
  }
  if (root.color_toolbar && needs(*root.color_toolbar)) {
    return true;
  }
  if (root.chat && needs(*root.chat)) {
    return true;
  }
  return false;
}

void ModelExecutor::PublishRoot(Window& root, PublicationChannel<3>& channel) {
  if (!RootNeedsPublish(root)) {
    return;
  }
  auto* buffer = channel.AcquireProducer();
  assert(buffer != nullptr);
  auto& last = root.obj_id.id() == app_.window_a.id().id() ? last_pub_a_
                                                           : last_pub_b_;
  SerializeWindowRoot(root, store_, last, buffer->sink);
  channel.NotePublished();
  channel.PublishProducer();
  if (notify_) {
    notify_(root.obj_id.id(), &channel);
  }
}

void ModelExecutor::PublishAllRoots() {
  app_.window_a.Load();
  app_.window_b.Load();
  PublishRoot(*app_.window_a, channel_a_);
  PublishRoot(*app_.window_b, channel_b_);
}

std::uint64_t ModelExecutor::publication_count(std::uint32_t root_id) const {
  if (root_id == app_.window_a.id().id()) {
    return channel_a_.publish_count();
  }
  if (root_id == app_.window_b.id().id()) {
    return channel_b_.publish_count();
  }
  return 0;
}

bool ModelExecutor::SaveShutdown() {  // runtime-save-ok: shutdown
  Application::ptr::MakeFromThis(&app_).Save();  // runtime-save-ok: shutdown
  return true;
}

}  // namespace apptraverse
