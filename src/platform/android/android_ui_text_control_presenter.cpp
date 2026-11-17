#include "apptraverse/platform/android/android_ui_text_control_presenter.h"
#include "android_jni_util.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"
namespace apptraverse::ui::android {
struct AndroidTextControlPresenter::ViewRefs {
  jobject view{};
};
void AndroidPresentationHost::LoadTextControl(AndroidTextControlPresenter& p) {
  auto* env = RequireUiThreadEnv(*this);
  auto cls = CheckedJni(env, "text class", [&] {
    return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  });
  auto method = CheckedJni(env, "create text method", [&] {
    return env->GetStaticMethodID(
        cls, "createTextControl",
        "(Landroid/view/ViewGroup;IIJ)Landroid/view/View;");
  });
  auto view = CheckedJni(env, "create text", [&] {
    return env->CallStaticObjectMethod(
        cls, method,
        ClientMount(*static_cast<AndroidDesktopWindowPresenter*>(
            p.window_presenter.operator->())),
        static_cast<jint>(p.control_id), static_cast<jint>(p.control->kind),
        reinterpret_cast<jlong>(&p));
  });
  p.view_refs_ =
      new AndroidTextControlPresenter::ViewRefs{env->NewGlobalRef(view)};
  env->DeleteLocalRef(view);
  env->DeleteLocalRef(cls);
  SyncTextControl(p);
}
void AndroidPresentationHost::SyncTextControl(AndroidTextControlPresenter& p) {
  auto* env = RequireUiThreadEnv(*this);
  auto cls = CheckedJni(env, "text class", [&] {
    return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  });
  auto method = CheckedJni(env, "sync text method", [&] {
    return env->GetStaticMethodID(
        cls, "updateTextControl",
        "(Landroid/view/View;Ljava/lang/String;Ljava/lang/String;ZZZZ)V");
  });
  std::string text = p.control->text;
  for (auto const& row : p.control->rows) {
    if (!text.empty()) text += '\n';
    text += row;
  }
  auto str = Utf8ToJstring(env, text);
  auto name = Utf8ToJstring(env, p.control->accessible_label);
  CheckedJni(env, "sync text", [&] {
    env->CallStaticVoidMethod(
        cls, method, p.view_refs_->view, str, name,
        static_cast<jboolean>(p.control->enabled),
        static_cast<jboolean>(p.control->visible),
        static_cast<jboolean>(p.control->read_only),
        static_cast<jboolean>(p.control->revision >= p.pending_edit));
  });
  env->DeleteLocalRef(str);
  env->DeleteLocalRef(name);
  env->DeleteLocalRef(cls);
}
void AndroidPresentationHost::UnloadTextControl(
    AndroidTextControlPresenter& p) {
  auto* env = RequireUiThreadEnv(*this);
  auto cls = CheckedJni(env, "text class", [&] {
    return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  });
  auto method = CheckedJni(env, "release text method", [&] {
    return env->GetStaticMethodID(cls, "releaseTextControl",
                                  "(Landroid/view/View;)V");
  });
  CheckedJni(env, "release text", [&] {
    env->CallStaticVoidMethod(cls, method, p.view_refs_->view);
  });
  env->DeleteLocalRef(cls);
  ClearGlobalRef(*this, p.view_refs_->view);
  delete p.view_refs_;
  p.view_refs_ = nullptr;
}
}  // namespace apptraverse::ui::android
