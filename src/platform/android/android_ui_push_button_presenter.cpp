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

void AndroidPresentationHost::LoadPushButton(
    AndroidPushButtonPresenter& presenter) {
  auto* desktop = static_cast<AndroidDesktopWindowPresenter*>(
      presenter.window_presenter.operator->());
  jobject parent = ClientMount(*desktop);
  if (parent == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidPushButtonPresenter::OnLoad missing parent mount\n");
    std::abort();
  }
  JNIEnv* env = RequireUiThreadEnv(*this);
  presenter.view_refs_ = new AndroidPushButtonViewRefs();
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID create = env->GetStaticMethodID(
      ui_class, "createPushButton",
      "(Landroid/view/ViewGroup;ILjava/lang/String;ZIIIIJ)Landroid/widget/Button;");
  ui::UiControlLayout const layout =
      presenter.window_presenter->ResolveChildLayout(
          {presenter.layout_x, presenter.layout_y, presenter.layout_width,
           presenter.layout_height, presenter.control_id});
  jstring jlabel = Utf8ToJstring(env, "");
  jobject button_view = env->CallStaticObjectMethod(
      ui_class, create, parent, static_cast<jint>(presenter.control_id), jlabel,
      JNI_TRUE, layout.x, layout.y, layout.width, layout.height,
      reinterpret_cast<jlong>(&presenter));
  presenter.view_refs_->button = env->NewGlobalRef(button_view);
  env->DeleteLocalRef(button_view);
  env->DeleteLocalRef(jlabel);
  env->DeleteLocalRef(ui_class);
  SyncPushButton(presenter);
}

void AndroidPresentationHost::SyncPushButton(
    AndroidPushButtonPresenter& presenter) {
  JNIEnv* env = RequireUiThreadEnv(*this);
  int const language_index =
      presenter.window_presenter->locale->language_index();
  std::string const label = presenter.button->label->Resolve(language_index);
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID update = env->GetStaticMethodID(
      ui_class, "updatePushButton",
      "(Landroid/widget/Button;Ljava/lang/String;Z)V");
  jstring jlabel = Utf8ToJstring(env, label.c_str());
  env->CallStaticVoidMethod(ui_class, update, presenter.view_refs_->button,
                            jlabel,
                            presenter.button->enabled() ? JNI_TRUE : JNI_FALSE);
  ui::UiControlLayout const layout =
      presenter.window_presenter->ResolveChildLayout(
          {presenter.layout_x, presenter.layout_y, presenter.layout_width,
           presenter.layout_height, presenter.control_id});
  jmethodID update_layout = env->GetStaticMethodID(
      ui_class, "updatePushButtonLayout",
      "(Landroid/widget/Button;IIII)V");
  env->CallStaticVoidMethod(ui_class, update_layout, presenter.view_refs_->button,
                            layout.x, layout.y, layout.width, layout.height);
  env->DeleteLocalRef(jlabel);
  env->DeleteLocalRef(ui_class);
}

void AndroidPresentationHost::UnloadPushButton(
    AndroidPushButtonPresenter& presenter) {
  if (presenter.view_refs_ == nullptr) {
    return;
  }
  if (presenter.view_refs_->button != nullptr) {
    JNIEnv* env = RequireUiThreadEnv(*this);
    jclass ui_class =
        env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
    jmethodID release = env->GetStaticMethodID(
        ui_class, "releasePushButton", "(Landroid/widget/Button;)V");
    env->CallStaticVoidMethod(ui_class, release, presenter.view_refs_->button);
    env->DeleteLocalRef(ui_class);
  }
  ClearGlobalRef(*this, presenter.view_refs_->button);
  delete presenter.view_refs_;
  presenter.view_refs_ = nullptr;
}

}  // namespace apptraverse::ui::android
