#ifndef APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_
#define APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "aether-objects/obj/idomain_storage.h"

#include "apptraverse/publication_channel.h"

namespace apptraverse {

class Application;

// Copied outer-window geometry. Not an Event, not an ae::Obj, not a Windows
// type. Exists only to cross GUI thread → model thread.
struct WindowChangedCommand {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

enum class PublicationKind {
  Initial,
  Incremental,
};

// Model-thread path shared by the Win32 app and headless tests. GUI may
// submit WindowChangedCommand, consume publications, and RequestStop.
// Native notify/completion handles stay in Windows code.
struct ModelSession {
  std::filesystem::path state_dir;
  PublicationChannel<3> channel;
  std::mutex mu;
  std::condition_variable cv;
  bool stop{false};
  // Latest unapplied geometry. A newer command replaces an older one.
  std::optional<WindowChangedCommand> pending_window_change;

  void RequestStop();
  void SubmitWindowChanged(WindowChangedCommand command);
  // on_published: required platform/test boundary. Called on the model thread
  // after the serialized buffer is published, without holding mu.
  void Run(std::function<void(PublicationKind)> on_published);
};

// Apply an incremental MainWindow publication into the existing GUI mirror
// and notify its existing presenter. Does not recreate the Domain or call
// InitializePresenters.
void ApplyMainWindowIncremental(std::vector<std::uint8_t> const& bytes,
                                 Application& ui_application,
                                 ae::IDomainStorage& ui_storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_MAIN_WINDOW_LIFECYCLE_H_
