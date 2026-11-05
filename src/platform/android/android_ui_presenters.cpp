#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_native_views.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"

namespace apptraverse::ui::android {
namespace {

AndroidUiNativeViews& NativeViews(Presenter const& presenter) {
  return *static_cast<AndroidUiNativeViews*>(presenter.presentation_host);
}

}  // namespace

void AndroidDesktopWindowPresenter::OnLoad() {
  NativeViews(*this).LoadDesktopWindow(*this);
}

void AndroidDesktopWindowPresenter::OnModelChanged() {
  NativeViews(*this).SyncDesktopWindow(*this);
}

void AndroidDesktopWindowPresenter::OnUnload() {
  NativeViews(*this).UnloadDesktopWindow(*this);
}

void AndroidPushButtonPresenter::OnLoad() {
  NativeViews(*this).LoadPushButton(*this);
}

void AndroidPushButtonPresenter::OnModelChanged() {
  NativeViews(*this).SyncPushButton(*this);
}

void AndroidPushButtonPresenter::OnUnload() {
  NativeViews(*this).UnloadPushButton(*this);
}

void AndroidListContainerPresenter::OnLoad() {
  NativeViews(*this).LoadListContainer(*this);
}

void AndroidListContainerPresenter::OnModelChanged() {
  NativeViews(*this).SyncListContainer(*this);
}

void AndroidListContainerPresenter::OnUnload() {
  NativeViews(*this).UnloadListContainer(*this);
}

void AndroidListContainerPresenter::HandleRowClickFromJava(int row_index) {
  if (applying_mirror_sync_) {
    return;
  }
  SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(row_index));
}

void NotifyAndroidPushButtonClickedFromJava(std::int64_t native_presenter) {
  auto* presenter =
      reinterpret_cast<AndroidPushButtonPresenter*>(native_presenter);
  presenter->NotifyClickedFromUi();
}

void NotifyAndroidListSelectionFromJava(std::int64_t native_presenter,
                                        std::int32_t row_index) {
  auto* presenter =
      reinterpret_cast<AndroidListContainerPresenter*>(native_presenter);
  presenter->HandleRowClickFromJava(row_index);
}

}  // namespace apptraverse::ui::android
