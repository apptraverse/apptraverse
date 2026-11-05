#include "apptraverse/platform/ios/ios_ui_presenters.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/ios/ios_ui_native_views.h"

namespace apptraverse::ui::ios {
namespace {

APPTRAVERSE_REGISTER(IosDesktopWindowPresenter);
APPTRAVERSE_REGISTER(IosPushButtonPresenter);
APPTRAVERSE_REGISTER(IosListContainerPresenter);

IosUiNativeViews& NativeViews(Presenter const& presenter) {
  return *static_cast<IosUiNativeViews*>(presenter.presentation_host);
}

}  // namespace

void EnsureIosUiPresenterRegistration() {
  (void)&g_apptraverse_registrar_IosDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_IosPushButtonPresenter;
  (void)&g_apptraverse_registrar_IosListContainerPresenter;
}

void IosDesktopWindowPresenter::OnLoad() {
  NativeViews(*this).LoadDesktopWindow(*this);
}

void IosDesktopWindowPresenter::OnModelChanged() {
  NativeViews(*this).SyncDesktopWindow(*this);
}

void IosDesktopWindowPresenter::OnUnload() {
  NativeViews(*this).UnloadDesktopWindow(*this);
}

void IosPushButtonPresenter::OnLoad() {
  NativeViews(*this).LoadPushButton(*this);
}

void IosPushButtonPresenter::OnModelChanged() {
  NativeViews(*this).SyncPushButton(*this);
}

void IosPushButtonPresenter::OnUnload() {
  NativeViews(*this).UnloadPushButton(*this);
}

void IosListContainerPresenter::OnLoad() {
  NativeViews(*this).LoadListContainer(*this);
}

void IosListContainerPresenter::OnModelChanged() {
  NativeViews(*this).SyncListContainer(*this);
}

void IosListContainerPresenter::OnUnload() {
  NativeViews(*this).UnloadListContainer(*this);
}

}  // namespace apptraverse::ui::ios
