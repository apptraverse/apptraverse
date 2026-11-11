#include "apptraverse/platform/android/android_ui_window_presenter.h"

#include "android_jni_util.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::android {

struct AndroidDesktopWindowViewRefs {
  jobject client_mount{nullptr};
};

jobject AndroidPresentationHost::ClientMount(
    AndroidDesktopWindowPresenter const& desktop) {
  return desktop.view_refs_->client_mount;
}

void AndroidPresentationHost::LoadDesktopWindow(
    AndroidDesktopWindowPresenter& presenter) {
  if (content_root == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidDesktopWindowPresenter::OnLoad missing host root\n");
    std::abort();
  }
  JNIEnv* env = RequireUiThreadEnv(*this);
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  if (ui_class == nullptr) {
    WriteFatalStderr("fatal: AppTraverseAndroidUi class missing\n");
    std::abort();
  }
  jmethodID create_client = env->GetStaticMethodID(
      ui_class, "createClientMount", "(Landroid/view/ViewGroup;J)Landroid/widget/FrameLayout;");
  if (create_client == nullptr) {
    WriteFatalStderr("fatal: createClientMount missing\n");
    std::abort();
  }
  presenter.view_refs_ = new AndroidDesktopWindowViewRefs();
  jobject client = env->CallStaticObjectMethod(
      ui_class, create_client, content_root, reinterpret_cast<jlong>(&presenter));
  presenter.view_refs_->client_mount = env->NewGlobalRef(client);
  env->DeleteLocalRef(client);
  env->DeleteLocalRef(ui_class);
  SyncDesktopWindow(presenter);
}

void NotifyAndroidWindowClientSizeFromJava(std::int64_t native_presenter,
                                         std::int32_t width,
                                         std::int32_t height,
                                         std::int32_t density_dpi) {
  auto& presenter = *reinterpret_cast<AndroidDesktopWindowPresenter*>(native_presenter);
  auto& host = *static_cast<AndroidPresentationHost*>(
      static_cast<AndroidUiNativeViews*>(presenter.presentation_host));
  if (host.on_window_client_size) {
    host.on_window_client_size(presenter, width, height, density_dpi);
  } else {
    presenter.ReportNativeFrameToModel(0, 0, width, height, false);
  }
}

void AndroidPresentationHost::SyncDesktopWindow(
    AndroidDesktopWindowPresenter& presenter) {
  if (activity == nullptr) {
    return;
  }
  JNIEnv* env = RequireUiThreadEnv(*this);
  std::string const title_text = presenter.ResolvePresentedWindowTitle(
      presenter.locale->language_index());
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID set_title = env->GetStaticMethodID(
      ui_class, "setActivityTitle", "(Landroid/app/Activity;Ljava/lang/String;)V");
  jstring jtitle = Utf8ToJstring(env, title_text.c_str());
  env->CallStaticVoidMethod(ui_class, set_title, activity, jtitle);
  env->DeleteLocalRef(jtitle);
  env->DeleteLocalRef(ui_class);
}

UiControlLayout AndroidDesktopWindowPresenter::ResolveChildLayout(
    UiControlLayout requested) const {
  // The model receives native viewport/DPI events and publishes pixel layouts.
  // Native presenters apply those layouts without a second layout calculation.
  return requested;
}

void AndroidPresentationHost::UnloadDesktopWindow(
    AndroidDesktopWindowPresenter& presenter) {
  JNIEnv* env = RequireUiThreadEnv(*this);
  jclass ui_class =
      env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID release = env->GetStaticMethodID(
      ui_class, "releaseClientMount", "(Landroid/widget/FrameLayout;)V");
  env->CallStaticVoidMethod(ui_class, release,
                            presenter.view_refs_->client_mount);
  env->DeleteLocalRef(ui_class);
  ClearGlobalRef(*this, presenter.view_refs_->client_mount);
  delete presenter.view_refs_;
  presenter.view_refs_ = nullptr;
}

}  // namespace apptraverse::ui::android

// Existing hosts may register only the original button/list callbacks. Keep
// the new viewport callback resolvable by JNI without changing those hosts.
extern "C" JNIEXPORT void JNICALL
Java_com_apptraverse_ui_android_AppTraverseAndroidUi_nativeOnWindowClientSize(
    JNIEnv*, jclass, jlong presenter, jint width, jint height, jint dpi) {
  apptraverse::ui::android::NotifyAndroidWindowClientSizeFromJava(
      presenter, width, height, dpi);
}
