#ifndef APPTRAVERSE_EXAMPLES_LINUX_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_LINUX_RUNTIME_H_

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace apptraverse::linux_host {

struct UiSink {
  std::function<void(std::string)> post_local_uid;
  std::function<void(std::string)> post_transcript;
  std::function<void(std::string)> post_error;
};

// Event-driven Linux host:
// GTK UI thread → business queue/thread → ChatComponent / RAM model
//              → network queue/thread → Aether / P2P
// and the reverse for receive + presentation.
class LinuxRuntime {
 public:
  LinuxRuntime(std::string state_dir, UiSink ui);
  ~LinuxRuntime();

  LinuxRuntime(LinuxRuntime const&) = delete;
  LinuxRuntime& operator=(LinuxRuntime const&) = delete;

  // Blocks on the caller thread until Stop() completes ordered shutdown.
  void Run();

  // GTK close / explicit stop. Safe to call more than once; work runs once.
  void Stop();

  // Enqueue from the GTK UI thread only.
  bool QueueSend(std::string text);
  bool QueueAddPeer(std::string uid);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace apptraverse::linux_host

#endif  // APPTRAVERSE_EXAMPLES_LINUX_RUNTIME_H_
