#include "main_window_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>
#endif

#include <cassert>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"

#include "main_window_ids.h"
#include "main_window_model.h"

namespace apptraverse {
namespace {

// Registrars live in this always-linked TU so both distill and load-only
// executables register the graph classes without a second Ensure* wrapper.
APPTRAVERSE_REGISTER(MainWindow);
APPTRAVERSE_REGISTER(MainWindowPresenter);
APPTRAVERSE_REGISTER(Application);
APPTRAVERSE_REGISTER(WindowChangedEvent);

}  // namespace

void ModelSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu};
    stop = true;
  }
  cv.notify_all();
}

void ModelSession::SubmitWindowChanged(WindowChangedCommand command) {
  {
    std::lock_guard<std::mutex> lock{mu};
    pending_window_change = command;
  }
  cv.notify_all();
}

// PUBLISH: in-memory model → RamDomainStorage scratch → publication buffer.
// Does not touch DirectoryDomainStorage.
void PublishWindowChange(ModelSession& session, MainWindow const& window,
                         std::uint64_t processed_sequence) {
  auto* buffer = session.channel.AcquireProducer();
  buffer->sink.write(&processed_sequence, sizeof(processed_sequence));
  SerializeIncrementalNodePublication(window, buffer->sink);
  {
    std::lock_guard<std::mutex> lock{session.mu};
    session.channel.NotePublished();
    session.channel.PublishProducer();
  }
  session.cv.notify_all();
}

void ApplyMainWindowIncremental(std::vector<std::uint8_t> const& bytes,
                                 Application& ui_application,
                                 ae::IDomainStorage& ui_storage) {
  // Keep the live presenter across deserialize. Loading the MainWindow
  // presenter ref drops its cache; without this hold the instance (and its
  // native HWND) would be released and replaced.
  auto window = ui_application.main_window;
  auto presenter = window->presenter;
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  std::uint64_t processed_sequence = 0;
  in.read(&processed_sequence, sizeof(processed_sequence));
  assert(in.ok);
  ae::Obj& updated =
      ApplyIncrementalPublication(in, *ui_application.domain, ui_storage);
  assert(&updated == &*window);
  assert(&*window->presenter == &*presenter);
  presenter->last_acknowledged_window_change_sequence = processed_sequence;
  presenter->OnModelChanged();
}

void ModelSession::Run(std::function<void(PublicationKind)> on_published) {
#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  // Development bootstrap: create persisted state only when it does not exist.
  bool state_missing = true;
  if (std::filesystem::exists(state_dir)) {
    DirectoryDomainStorage probe{state_dir};
    state_missing =
        probe
            .Enumerate(ae::ObjId{main_window::ToObjId(
                main_window::ObjId::Application)})
            .empty();
  }
  if (state_missing) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildMainWindowGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }
#endif

  {
    DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    auto application = LoadApplication<Application>(
        domain,
        ae::ObjId{main_window::ToObjId(main_window::ObjId::Application)});

    // MainWindow resize history is not needed for sync or debug replay.
    application->main_window->SetJournalRetentionPolicy(
        JournalRetentionPolicy{.max_events = 0});

    auto* buffer = channel.AcquireProducer();
    SerializeInitialPublication(*application, buffer->sink);
    {
      std::lock_guard<std::mutex> lock{mu};
      channel.NotePublished();
      channel.PublishProducer();
    }
    cv.notify_all();
    on_published(PublicationKind::Initial);

    for (;;) {
      WindowChangedCommand command;
      {
        std::unique_lock<std::mutex> lock{mu};
        cv.wait(lock, [&] {
          return stop || (pending_window_change.has_value() &&
                          !channel.has_unread_published());
        });
        if (stop) {
          break;
        }
        command = *pending_window_change;
        pending_window_change.reset();
      }

      MainWindow& window = *application->main_window;
      if (window.x != command.x || window.y != command.y ||
          window.width != command.width || window.height != command.height) {
        auto event =
            WindowChangedEvent::ptr::Create(ae::CreateWith{*window.domain});
        event->x = command.x;
        event->y = command.y;
        event->width = command.width;
        event->height = command.height;
        window.Commit(event);
      }

      // Acknowledge every taken command, including a geometry no-op, so the
      // GUI can tell that this native input has been considered.
      PublishWindowChange(*this, window, command.sequence);
      on_published(PublicationKind::Incremental);
    }

    // PERSIST: compact then one graph save. Runtime commits stay in memory
    // until here. Compaction is housekeeping before disk write.
    application->main_window->CompactJournal(SystemUtcMicros());
    application.Save();
  }
}

}  // namespace apptraverse
