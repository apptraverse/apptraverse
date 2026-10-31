#include "apptraverse/platform/android/android_ui_list_presenter.h"

#include "android_jni_util.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::android {

struct AndroidListContainerViewRefs {
  jobject list{nullptr};
};

void NotifyAndroidListSelectionFromJava(std::int64_t native_presenter,
                                        jint row_index) {
  auto* presenter =
      reinterpret_cast<AndroidListContainerPresenter*>(native_presenter);
  presenter->HandleRowClickFromJava(row_index);
}

void AndroidListContainerPresenter::HandleRowClickFromJava(int row_index) {
  if (applying_mirror_sync_) {
    return;
  }
  SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(row_index));
}

void AndroidListContainerPresenter::OnLoad() {
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  auto* desktop = static_cast<AndroidDesktopWindowPresenter*>(
      window_presenter.operator->());
  jobject parent = desktop->ClientMount();
  if (host == nullptr || parent == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidListContainerPresenter::OnLoad missing parent mount\n");
    std::abort();
  }
  JNIEnv* env = RequireUiThreadEnv(*host);
  view_refs_ = new AndroidListContainerViewRefs();
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID create = env->GetStaticMethodID(
      ui_class, "createListContainer",
      "(Landroid/view/ViewGroup;IIIII)Landroid/widget/LinearLayout;");
  ui::UiControlLayout const layout =
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});
  jobject list_view = env->CallStaticObjectMethod(
      ui_class, create, parent, static_cast<jint>(control_id), layout.x,
      layout.y, layout.width, layout.height);
  view_refs_->list = env->NewGlobalRef(list_view);
  env->DeleteLocalRef(list_view);
  env->DeleteLocalRef(ui_class);
  SyncFromMirror();
}

void AndroidListContainerPresenter::SyncFromMirror() {
  if (view_refs_ == nullptr || view_refs_->list == nullptr || !container) {
    return;
  }
  applying_mirror_sync_ = true;
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  JNIEnv* env = RequireUiThreadEnv(*host);
  container.Load();
  window_presenter.Load();
  window_presenter->locale.Load();
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID bind_row = env->GetStaticMethodID(
      ui_class, "bindListRow",
      "(Landroid/widget/LinearLayout;ILjava/lang/String;ZJ)V");
  int const language_index = window_presenter->locale->language_index();
  int const selection = container->selection_index;
  std::vector<std::string> const labels =
      ResolvePresentedListRowPrimaryTexts(*container, language_index);
  for (std::size_t i = 0; i < labels.size(); ++i) {
    std::string const& label = labels[i];
    jstring jlabel = Utf8ToJstring(env, label.c_str());
    env->CallStaticVoidMethod(
        ui_class, bind_row, view_refs_->list, static_cast<jint>(i), jlabel,
        (selection >= 0 && static_cast<std::size_t>(selection) == i) ? JNI_TRUE
                                                                     : JNI_FALSE,
        reinterpret_cast<jlong>(this));
    env->DeleteLocalRef(jlabel);
  }
  jmethodID clear_rows = env->GetStaticMethodID(
      ui_class, "clearListRows", "(Landroid/widget/LinearLayout;I)V");
  env->CallStaticVoidMethod(
      ui_class, clear_rows, view_refs_->list,
      static_cast<jint>(container->rows.size()));
  env->DeleteLocalRef(ui_class);
  applying_mirror_sync_ = false;
}

void AndroidListContainerPresenter::OnModelChanged() { SyncFromMirror(); }

void AndroidListContainerPresenter::OnUnload() {
  if (view_refs_ == nullptr) {
    return;
  }
  auto* host = static_cast<AndroidPresentationHost*>(presentation_host);
  if (view_refs_->list != nullptr) {
    JNIEnv* env = RequireUiThreadEnv(*host);
    jclass view_class = env->FindClass("android/view/View");
    jmethodID get_parent = env->GetMethodID(
        view_class, "getParent", "()Landroid/view/ViewParent;");
    jobject parent_obj = env->CallObjectMethod(view_refs_->list, get_parent);
    if (parent_obj != nullptr) {
      jclass vg_class = env->FindClass("android/view/ViewGroup");
      jmethodID remove_view = env->GetMethodID(
          vg_class, "removeView", "(Landroid/view/View;)V");
      env->CallVoidMethod(parent_obj, remove_view, view_refs_->list);
      env->DeleteLocalRef(parent_obj);
      env->DeleteLocalRef(vg_class);
    }
    env->DeleteLocalRef(view_class);
  }
  ClearGlobalRef(*host, view_refs_->list);
  delete view_refs_;
  view_refs_ = nullptr;
}

}  // namespace apptraverse::ui::android
