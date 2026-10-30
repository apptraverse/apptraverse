#include "apptraverse/platform/android/android_ui_push_button_presenter.h"

#include "android_jni_util.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::android {

struct AndroidPushButtonViewRefs {
  jobject button{nullptr};
};

void NotifyAndroidPushButtonClickedFromJava(std::int64_t native_presenter) {
  auto* presenter =
      reinterpret_cast<AndroidPushButtonPresenter*>(native_presenter);
  presenter->NotifyClickedFromUi();
}

void AndroidPushButtonPresenter::OnLoad() {
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  auto* desktop = static_cast<AndroidDesktopWindowPresenter*>(
      window_presenter.operator->());
  jobject parent = desktop->ClientMount();
  if (host == nullptr || parent == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidPushButtonPresenter::OnLoad missing parent mount\n");
    std::abort();
  }
  JNIEnv* env = RequireUiThreadEnv(*host);
  view_refs_ = new AndroidPushButtonViewRefs();
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID create = env->GetStaticMethodID(
      ui_class, "createPushButton",
      "(Landroid/view/ViewGroup;ILjava/lang/String;ZIIIIJ)Landroid/widget/Button;");
  ui::UiControlLayout const layout =
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});
  jstring jlabel = Utf8ToJstring(env, "");
  jobject button_view = env->CallStaticObjectMethod(
      ui_class, create, parent, static_cast<jint>(control_id), jlabel,
      JNI_TRUE, layout.x, layout.y, layout.width, layout.height,
      reinterpret_cast<jlong>(this));
  view_refs_->button = env->NewGlobalRef(button_view);
  env->DeleteLocalRef(button_view);
  env->DeleteLocalRef(jlabel);
  env->DeleteLocalRef(ui_class);
  SyncFromMirror();
}

void AndroidPushButtonPresenter::SyncFromMirror() {
  if (view_refs_ == nullptr || view_refs_->button == nullptr) {
    return;
  }
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  JNIEnv* env = RequireUiThreadEnv(*host);
  button.Load();
  window_presenter.Load();
  window_presenter->locale.Load();
  button->label.Load();
  if (!button->label) {
    WriteFatalStderr(
        "fatal: AndroidPushButtonPresenter mirror label not found\n");
    std::abort();
  }
  int const language_index = window_presenter->locale->language_index();
  std::string const label = button->label->Resolve(language_index);
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID update = env->GetStaticMethodID(
      ui_class, "updatePushButton",
      "(Landroid/widget/Button;Ljava/lang/String;Z)V");
  jstring jlabel = Utf8ToJstring(env, label.c_str());
  env->CallStaticVoidMethod(ui_class, update, view_refs_->button, jlabel,
                            button->enabled() ? JNI_TRUE : JNI_FALSE);
  env->DeleteLocalRef(jlabel);
  env->DeleteLocalRef(ui_class);
}

void AndroidPushButtonPresenter::OnModelChanged() { SyncFromMirror(); }

void AndroidPushButtonPresenter::OnUnload() {
  if (view_refs_ == nullptr) {
    return;
  }
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  if (view_refs_->button != nullptr) {
    JNIEnv* env = RequireUiThreadEnv(*host);
    jclass view_class = env->FindClass("android/view/View");
    jmethodID remove = env->GetMethodID(
        view_class, "getParent", "()Landroid/view/ViewParent;");
    jobject parent_obj = env->CallObjectMethod(view_refs_->button, remove);
    if (parent_obj != nullptr) {
      jclass vg_class = env->FindClass("android/view/ViewGroup");
      jmethodID remove_view = env->GetMethodID(
          vg_class, "removeView", "(Landroid/view/View;)V");
      env->CallVoidMethod(parent_obj, remove_view, view_refs_->button);
      env->DeleteLocalRef(parent_obj);
      env->DeleteLocalRef(vg_class);
    }
    env->DeleteLocalRef(view_class);
  }
  ClearGlobalRef(*host, view_refs_->button);
  delete view_refs_;
  view_refs_ = nullptr;
}

}  // namespace apptraverse::ui::android
