#ifndef APPTRAVERSE_SURFACES_LINUX_APP_H_
#define APPTRAVERSE_SURFACES_LINUX_APP_H_

#include <X11/Xlib.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"

#include "surfaces_lifecycle.h"
#include "surfaces_model.h"

namespace apptraverse {

class LinuxSurfacePresenter;

class LinuxApp {
 public:
  int Run(std::filesystem::path const& state_dir);

  Display* display() const { return display_; }
  Atom wm_delete() const { return wm_delete_; }
  Atom wm_protocols() const { return wm_protocols_; }
  Atom net_frame_extents() const { return net_frame_extents_; }

  void RegisterPresenter(Window window, LinuxSurfacePresenter* presenter);
  void UnregisterPresenter(Window window);
  LinuxSurfacePresenter* PresenterFor(Window window) const;

  // GUI-thread: wake-pipe STOP (geometry snapshot then model RequestStop).
  void RequestApplicationStop();
  // Any thread: enqueue STOP onto the GUI wake pipe.
  void PostApplicationStop();

  // Test/helper: place a Surface window to an outer-frame rect (best-effort).
  void PlaceOuterWindow(Window window, int x, int y, int width, int height);
  // Test/helper: read outer-frame rect (best-effort).
  void ReadOuterBounds(Window window, int* x, int* y, int* width,
                       int* height) const;

 private:
  void OpenDisplay();
  void CloseDisplay();
  void CreateWakePipe();
  void CloseWakePipe();
  void WriteWake(std::uint8_t code);
  void DrainWake();
  void OnInitialPublished();
  void OnIncrementalPublished();
  void QueueAllWindowBounds();
  void CreateLoadingWindow();
  void DestroyLoadingWindow();
  void PaintLoading();
  void DispatchXEvent(XEvent const& event);
  bool QueryFrameExtents(Window window, long* left, long* right, long* top,
                         long* bottom) const;
  void ApplyOuterPlacement(Window window, int outer_x, int outer_y,
                           int outer_w, int outer_h);

  SurfacesModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::thread model_thread_;
  Display* display_{nullptr};
  Window root_{None};
  Window loading_{None};
  Atom wm_delete_{None};
  Atom wm_protocols_{None};
  Atom net_frame_extents_{None};
  int wake_pipe_[2]{-1, -1};
  std::atomic<bool> model_done_{false};
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
  std::unordered_map<Window, LinuxSurfacePresenter*> presenters_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_LINUX_APP_H_
