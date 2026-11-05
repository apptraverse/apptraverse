#include "apptraverse/platform/macos/macos_ui_presenters.h"

#include "apptraverse/object_macros.h"
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

}  // namespace apptraverse::ui::macos
