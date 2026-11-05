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

void AndroidPresentationHost::LoadListContainer(
    AndroidListContainerPresenter& presenter) {
  auto* desktop = static_cast<AndroidDesktopWindowPresenter*>(
      presenter.window_presenter.operator->());
  jobject parent = ClientMount(*desktop);
  if (parent == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidListContainerPresenter::OnLoad missing parent mount\n");
    std::abort();
  }
  JNIEnv* env = RequireUiThreadEnv(*this);
  presenter.view_refs_ = new AndroidListContainerViewRefs();
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID create = env->GetStaticMethodID(
      ui_class, "createListContainer",
      "(Landroid/view/ViewGroup;IIIII)Landroid/widget/LinearLayout;");
  ui::UiControlLayout const layout =
      presenter.window_presenter->ResolveChildLayout(
          {presenter.layout_x, presenter.layout_y, presenter.layout_width,
           presenter.layout_height, presenter.control_id});
  jobject list_view = env->CallStaticObjectMethod(
      ui_class, create, parent, static_cast<jint>(presenter.control_id),
      layout.x, layout.y, layout.width, layout.height);
  presenter.view_refs_->list = env->NewGlobalRef(list_view);
  env->DeleteLocalRef(list_view);
  env->DeleteLocalRef(ui_class);
  SyncListContainer(presenter);
}

void AndroidPresentationHost::SyncListContainer(
    AndroidListContainerPresenter& presenter) {
  presenter.applying_mirror_sync_ = true;
  JNIEnv* env = RequireUiThreadEnv(*this);
  jclass ui_class = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID bind_row = env->GetStaticMethodID(
      ui_class, "bindListRow",
      "(Landroid/widget/LinearLayout;ILjava/lang/String;ZJ)V");
  int const language_index =
      presenter.window_presenter->locale->language_index();
  int const selection = presenter.container->selection_index;
  std::vector<std::string> const labels =
      ResolvePresentedListRowPrimaryTexts(*presenter.container, language_index);
  for (std::size_t i = 0; i < labels.size(); ++i) {
    std::string const& label = labels[i];
    jstring jlabel = Utf8ToJstring(env, label.c_str());
    env->CallStaticVoidMethod(
        ui_class, bind_row, presenter.view_refs_->list, static_cast<jint>(i),
        jlabel,
        (selection >= 0 && static_cast<std::size_t>(selection) == i) ? JNI_TRUE
                                                                     : JNI_FALSE,
        reinterpret_cast<jlong>(&presenter));
    env->DeleteLocalRef(jlabel);
  }
  jmethodID clear_rows = env->GetStaticMethodID(
      ui_class, "clearListRows", "(Landroid/widget/LinearLayout;I)V");
  env->CallStaticVoidMethod(
      ui_class, clear_rows, presenter.view_refs_->list,
      static_cast<jint>(presenter.container->rows.size()));
  env->DeleteLocalRef(ui_class);
  presenter.applying_mirror_sync_ = false;
}

void AndroidPresentationHost::UnloadListContainer(
    AndroidListContainerPresenter& presenter) {
  if (presenter.view_refs_ == nullptr) {
    return;
  }
  if (presenter.view_refs_->list != nullptr) {
    JNIEnv* env = RequireUiThreadEnv(*this);
    jclass ui_class =
        env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
    jmethodID release = env->GetStaticMethodID(
        ui_class, "releaseListContainer", "(Landroid/widget/LinearLayout;)V");
    env->CallStaticVoidMethod(ui_class, release, presenter.view_refs_->list);
    env->DeleteLocalRef(ui_class);
  }
  ClearGlobalRef(*this, presenter.view_refs_->list);
  delete presenter.view_refs_;
  presenter.view_refs_ = nullptr;
}

}  // namespace apptraverse::ui::android
