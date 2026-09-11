#ifndef APPTRAVERSE_SURFACES_LINUX_APP_H_
#define APPTRAVERSE_SURFACES_LINUX_APP_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

#include <gtk/gtk.h>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"

#include "surfaces_lifecycle.h"
#include "surfaces_model.h"

namespace apptraverse {

class LinuxSurfacePresenter;

gboolean LinuxAppWakeIdle(gpointer data);
gboolean LinuxAppGuiInvokeIdle(gpointer data);

class LinuxApp {
 public:
  int Run(std::filesystem::path const& state_dir);

  void RegisterPresenter(GtkWidget* window, LinuxSurfacePresenter* presenter);
  void UnregisterPresenter(GtkWidget* window);
  LinuxSurfacePresenter* PresenterFor(GtkWidget* window) const;

  void RequestApplicationStop();
  void PostApplicationStop();

  void RestoreActiveSurfaceZOrder();

  // Schedule work on the GTK main thread (safe from any thread).
  void InvokeOnGui(std::function<void()> fn);

  void PlaceOuterWindow(GtkWidget* window, int x, int y, int width, int height);
  void ReadOuterBounds(GtkWidget* window, int* x, int* y, int* width,
                       int* height) const;

 private:
  friend gboolean LinuxAppWakeIdle(gpointer data);
  friend gboolean LinuxAppGuiInvokeIdle(gpointer data);

  void HandleWake(std::uint8_t code);
  void PostWake(std::uint8_t code);
  void OnInitialPublished();
  void OnIncrementalPublished();
  void OnModelFinished();
  void QueueAllWindowBounds();
  void QueueFocusedAsCurrent();
  void CreateLoadingWindow();
  void DestroyLoadingWindow();

  SurfacesModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::thread model_thread_;
  GtkWidget* loading_{nullptr};
  std::atomic<bool> model_done_{false};
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
  std::unordered_map<GtkWidget*, LinuxSurfacePresenter*> presenters_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_LINUX_APP_H_
