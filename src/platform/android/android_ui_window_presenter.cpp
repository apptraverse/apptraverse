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

jobject AndroidDesktopWindowPresenter::ClientMount() const {
  if (view_refs_ == nullptr) {
    return nullptr;
  }
  return view_refs_->client_mount;
}

void AndroidDesktopWindowPresenter::OnLoad() {
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  if (host == nullptr || host->content_root == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidDesktopWindowPresenter::OnLoad missing host root\n");
    std::abort();
  }
  JNIEnv* env = RequireUiThreadEnv(*host);
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
  view_refs_ = new AndroidDesktopWindowViewRefs();
  jobject client = env->CallStaticObjectMethod(
      ui_class, create_client, host->content_root);
  view_refs_->client_mount = env->NewGlobalRef(client);
  env->DeleteLocalRef(client);
  env->DeleteLocalRef(ui_class);
  SyncTitleFromMirror();
}

void AndroidDesktopWindowPresenter::SyncTitleFromMirror() {
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  if (host == nullptr || host->activity == nullptr || view_refs_ == nullptr) {
    return;
  }
  JNIEnv* env = RequireUiThreadEnv(*host);
  std::string title_text;
  if (title.is_valid()) {
    title.Load();
    if (locale.is_valid()) {
      locale.Load();
    }
    int language_index = locale ? locale->language_index() : 0;
    title_text = title->Resolve(language_index);
  }
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID set_title = env->GetStaticMethodID(
      ui_class, "setActivityTitle", "(Landroid/app/Activity;Ljava/lang/String;)V");
  jstring jtitle = Utf8ToJstring(env, title_text.c_str());
  env->CallStaticVoidMethod(ui_class, set_title, host->activity, jtitle);
  env->DeleteLocalRef(jtitle);
  env->DeleteLocalRef(ui_class);
}

void AndroidDesktopWindowPresenter::OnModelChanged() { SyncTitleFromMirror(); }

void AndroidDesktopWindowPresenter::OnUnload() {
  if (view_refs_ == nullptr) {
    return;
  }
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  ClearGlobalRef(*host, view_refs_->client_mount);
  delete view_refs_;
  view_refs_ = nullptr;
}

}  // namespace apptraverse::ui::android
