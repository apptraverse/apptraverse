#ifndef APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_
#define APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>

#include "apptraverse/publication_channel.h"

namespace apptraverse {

// Model-thread path shared by the Win32 app and headless tests. GUI may only
// touch the publication channel and RequestStop. Native notify/completion
// handles stay in Windows code; publication is reported through on_published.
struct ModelSession {
  std::filesystem::path state_dir;
  PublicationChannel<3> channel;
  std::mutex mu;
  std::condition_variable cv;
  bool stop{false};

  void RequestStop();
  // on_published: required platform/test boundary. Called on the model thread
  // after the serialized buffer is published, without holding mu.
  void Run(std::function<void()> on_published);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_
