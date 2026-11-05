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
