#include "apptraverse/platform/macos/macos_ui_presenters.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include <cstdlib>
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/macos/macos_ui_native_views.h"

namespace apptraverse::ui::macos {
namespace {

APPTRAVERSE_REGISTER(MacDesktopWindowPresenter);
APPTRAVERSE_REGISTER(MacPushButtonPresenter);
APPTRAVERSE_REGISTER(MacListContainerPresenter);

MacUiNativeViews& NativeViews(Presenter const& presenter) {
  return *static_cast<MacUiNativeViews*>(presenter.presentation_host);
}

}  // namespace

void EnsureMacUiPresenterRegistration() {
  EnsureUiControlRegistration();
  (void)&g_apptraverse_registrar_MacDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_MacPushButtonPresenter;
  (void)&g_apptraverse_registrar_MacListContainerPresenter;
}

void MacDesktopWindowPresenter::OnLoad() {
  NativeViews(*this).LoadDesktopWindow(*this);
}

void MacDesktopWindowPresenter::OnModelChanged() {
  NativeViews(*this).SyncDesktopWindow(*this);
}

void MacDesktopWindowPresenter::ApplyRequestedFrameFromMirror() {
  NativeViews(*this).ApplyRequestedDesktopFrame(*this);
}

void MacDesktopWindowPresenter::ReportNativeFrameToModel(
    std::int32_t x, std::int32_t y, std::int32_t client_width,
    std::int32_t client_height, bool updates_restore_geometry) {
  ae::ObjId const id = window.id();
  model_proxy->EnqueueModelWork(
      [id, x, y, client_width, client_height,
       updates_restore_geometry](ae::Domain& domain) {
        // A native event accepted before model Close can execute after the
        // window has unloaded. This is a queued lifecycle state, not a missing
        // required graph link; do not revive the closed window to report it.
        auto object = domain.Find(id);
        if (!object) return;
        object.as<UiDesktopWindow>()->SubmitFrameFromNative(
            x, y, client_width, client_height, updates_restore_geometry);
      });
}

void MacDesktopWindowPresenter::SubmitScreenFromNative(
    std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  ae::ObjId const id = window.id();
  model_proxy->EnqueueModelWork([id, x, y, width, height](ae::Domain& domain) {
    auto object = domain.Find(id);
    if (!object) return;
    object.as<UiDesktopWindow>()->SubmitScreenFromNative(x, y, width, height);
  });
}

void MacDesktopWindowPresenter::ReportNativeScreenToModel(
    std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  SubmitScreenFromNative(x, y, width, height);
}

void MacDesktopWindowPresenter::OnUnload() {
  NativeViews(*this).UnloadDesktopWindow(*this);
}

void MacPushButtonPresenter::OnLoad() {
  NativeViews(*this).LoadPushButton(*this);
}

void MacPushButtonPresenter::OnModelChanged() {
  NativeViews(*this).SyncPushButton(*this);
}

void MacPushButtonPresenter::OnUnload() {
  NativeViews(*this).UnloadPushButton(*this);
}

void MacListContainerPresenter::OnLoad() {
  NativeViews(*this).LoadListContainer(*this);
}

void MacListContainerPresenter::OnModelChanged() {
  NativeViews(*this).SyncListContainer(*this);
}

void MacListContainerPresenter::OnUnload() {
  NativeViews(*this).UnloadListContainer(*this);
}

namespace {
std::vector<MacDesktopWindowPresenter*> LiveDesktopPresenters(ae::Obj& root) {
  std::vector<ae::Obj*> objects;
  CollectLiveReachableObjects(root, objects);
  std::vector<MacDesktopWindowPresenter*> presenters;
  auto& registry = ae::Registry::GetRegistry();
  for (auto* object : objects) {
    if (registry.GenerationDistance(MacDesktopWindowPresenter::kClassId,
                                    object->GetClassId()) >= 0) {
      auto* presenter = static_cast<MacDesktopWindowPresenter*>(object);
      if (presenter->presentation_loaded) presenters.push_back(presenter);
    }
  }
  return presenters;
}
}  // namespace

void FocusMacDesktopWindow(ae::Obj& root, ae::ObjId window_id) {
  for (auto* presenter : LiveDesktopPresenters(root)) {
    if (presenter->window.id() == window_id) {
      NativeViews(*presenter).OrderDesktopWindowFront(*presenter, true);
      return;
    }
  }
  WriteFatalStderr("macOS focus target has no active desktop presenter\n");
  std::abort();
}

std::vector<std::uint32_t> CaptureMacDesktopZOrder(ae::Obj& root) {
  auto const presenters = LiveDesktopPresenters(root);
  std::vector<std::uint32_t> result;
  if (presenters.empty()) return result;
  for (auto* presenter : NativeViews(*presenters.front()).DesktopZOrder()) {
    result.push_back(presenter->window.id().id());
  }
  return result;
}

void RestoreMacDesktopZOrder(ae::Obj& root,
                            std::vector<std::uint32_t> const& top_to_bottom) {
  auto const presenters = LiveDesktopPresenters(root);
  for (auto id = top_to_bottom.rbegin(); id != top_to_bottom.rend(); ++id) {
    for (auto* presenter : presenters) {
      if (presenter->window.id().id() == *id) {
        NativeViews(*presenter).OrderDesktopWindowFront(*presenter, false);
        break;
      }
    }
  }
}

}  // namespace apptraverse::ui::macos
