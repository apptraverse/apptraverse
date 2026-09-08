#include "main_window_lifecycle.h"

#include <cassert>
#include <memory>
#include <thread>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"

#include "main_window_ids.h"
#include "main_window_model.h"

namespace apptraverse {
namespace {

bool EnterStage(ModelSession& session, ModelStartupStage stage) {
  session.stage.store(static_cast<int>(stage), std::memory_order_release);
  session.cv.notify_all();
  std::unique_lock<std::mutex> lock{session.mu};
  session.cv.wait(lock, [&] {
    return session.stop.load(std::memory_order_acquire) ||
           session.hold_stage.load(std::memory_order_acquire) !=
               static_cast<int>(stage);
  });
  return !session.stop.load(std::memory_order_acquire);
}

}  // namespace

bool ApplicationStateExists(std::filesystem::path const& dir) {
  if (!std::filesystem::exists(dir)) {
    return false;
  }
  DirectoryDomainStorage storage{dir};
  auto const classes = storage.Enumerate(
      ae::ObjId{main_window::ToObjId(main_window::ObjId::Application)});
  return !classes.empty();
}

void ModelSession::RequestStop() {
  stop.store(true, std::memory_order_release);
  cv.notify_all();
}

void ModelSession::Run() {
  EnsureMainWindowRegistration();
  create_thread.store(std::this_thread::get_id(), std::memory_order_release);

  std::unique_ptr<DirectoryDomainStorage> storage;
  std::unique_ptr<ae::Domain> domain;
  Application::ptr application;
  bool const existing = ApplicationStateExists(state_dir);

  auto cleanup = [&] {
    static_cast<void>(EnterStage(*this, ModelStartupStage::Stopping));
    application = {};
    domain.reset();
    storage.reset();
    destroy_thread.store(std::this_thread::get_id(), std::memory_order_release);
    stage.store(static_cast<int>(ModelStartupStage::Stopped),
                std::memory_order_release);
    finished.store(true, std::memory_order_release);
#ifdef _WIN32
    if (done_event != nullptr) {
      SetEvent(done_event);
    }
#endif
    cv.notify_all();
  };

  if (!EnterStage(*this, ModelStartupStage::Creating)) {
    cleanup();
    return;
  }

  if (!existing) {
    storage = std::make_unique<DirectoryDomainStorage>(state_dir);
    domain = std::make_unique<ae::Domain>(*storage);
    application = BuildMainWindowGraph(*domain);

    if (!EnterStage(*this, ModelStartupStage::Distilling)) {
      cleanup();
      return;
    }
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);
    distilled_this_run.store(true, std::memory_order_release);

    if (!EnterStage(*this, ModelStartupStage::DestroyingFresh)) {
      cleanup();
      return;
    }
    application = {};
    domain.reset();
    storage.reset();
  }

  if (!EnterStage(*this, ModelStartupStage::Loading)) {
    cleanup();
    return;
  }
  storage = std::make_unique<DirectoryDomainStorage>(state_dir);
  domain = std::make_unique<ae::Domain>(*storage);
  application = LoadApplication<Application>(
      *domain, ae::ObjId{main_window::ToObjId(main_window::ObjId::Application)});

  if (!EnterStage(*this, ModelStartupStage::Serializing)) {
    cleanup();
    return;
  }
  auto* buffer = channel.AcquireProducer();
  assert(buffer != nullptr);
  SerializeInitialPublication(*application, buffer->sink);
  if (stop.load(std::memory_order_acquire)) {
    cleanup();
    return;
  }
  model_application_addr.store(reinterpret_cast<std::uintptr_t>(&*application),
                                std::memory_order_release);
  model_window_addr.store(
      reinterpret_cast<std::uintptr_t>(&*application->main_window),
      std::memory_order_release);
  model_domain_addr.store(reinterpret_cast<std::uintptr_t>(domain.get()),
                           std::memory_order_release);
  model_application_id.store(application->obj_id.id(), std::memory_order_release);
  model_window_id.store(application->main_window->obj_id.id(),
                         std::memory_order_release);
  model_window_x.store(application->main_window->x, std::memory_order_release);
  model_window_y.store(application->main_window->y, std::memory_order_release);
  model_window_width.store(application->main_window->width,
                           std::memory_order_release);
  model_window_height.store(application->main_window->height,
                            std::memory_order_release);
  model_window_dpi.store(application->main_window->dpi, std::memory_order_release);
  channel.NotePublished();
  channel.PublishProducer();
  published.store(true, std::memory_order_release);
#ifdef _WIN32
  auto const hwnd =
      reinterpret_cast<HWND>(notify_hwnd.load(std::memory_order_acquire));
  if (hwnd != nullptr) {
    PostMessageW(hwnd, WM_APPTRAVERSE_PUBLISHED, 0, 0);
  }
#endif
  cv.notify_all();

  if (!EnterStage(*this, ModelStartupStage::Ready)) {
    cleanup();
    return;
  }
  {
    std::unique_lock<std::mutex> lock{mu};
    cv.wait(lock, [&] { return stop.load(std::memory_order_acquire); });
  }
  cleanup();
}

}  // namespace apptraverse
