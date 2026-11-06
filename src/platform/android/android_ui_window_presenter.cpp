#include "apptraverse/platform/android/android_ui_window_presenter.h"

#include "android_jni_util.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_layout_scale.h"
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
      ui_class, "createClientMount", "(Landroid/view/ViewGroup;)Landroid/widget/FrameLayout;");
  if (create_client == nullptr) {
    WriteFatalStderr("fatal: createClientMount missing\n");
    std::abort();
  }
  presenter.view_refs_ = new AndroidDesktopWindowViewRefs();
  jobject client = env->CallStaticObjectMethod(
      ui_class, create_client, content_root);
  presenter.view_refs_->client_mount = env->NewGlobalRef(client);
  env->DeleteLocalRef(client);
  env->DeleteLocalRef(ui_class);
  SyncDesktopWindow(presenter);
}

void AndroidPresentationHost::SyncDesktopWindow(
    AndroidDesktopWindowPresenter& presenter) {
  if (activity == nullptr) {
    return;
  }
  JNIEnv* env = RequireUiThreadEnv(*this);
  std::string title_text;
  if (presenter.title.is_valid()) {
    title_text = presenter.title->Resolve(presenter.locale->language_index());
  }
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
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  if (host == nullptr || host->content_root == nullptr) {
    return requested;
  }
  auto desktop = desktop_window();
  if (!desktop.is_valid()) {
    return requested;
  }
  if (!desktop.is_loaded()) {
    desktop.Load();
  }
  std::int32_t model_w = desktop->client_width();
  std::int32_t model_h = desktop->client_height();
  if (model_w <= 0) {
    model_w = desktop->restore_client_width();
  }
  if (model_h <= 0) {
    model_h = desktop->restore_client_height();
  }
  if (model_w <= 0 || model_h <= 0) {
    return requested;
  }
  JNIEnv* env = RequireUiThreadEnv(*host);
  jobject measure_target = host->content_root;
  if (view_refs_ != nullptr && view_refs_->client_mount != nullptr) {
    measure_target = view_refs_->client_mount;
  }
  ViewContentSize pres = ReadViewContentSize(env, measure_target);
  if (pres.width <= 0 || pres.height <= 0) {
    pres = ReadViewContentSize(env, host->content_root);
  }
  if (pres.width <= 0 || pres.height <= 0) {
    return requested;
  }
  std::int32_t dpi = ReadActivityDensityDpi(env, host->activity);
  if (dpi <= 0) {
    dpi = 160;
  }
  return ScaleControlLayoutToPresentation(requested, model_w, model_h, pres.width,
                                          pres.height, dpi);
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
