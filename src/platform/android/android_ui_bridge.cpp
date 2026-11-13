#include "apptraverse/platform/android/android_ui_bridge.h"

#include "android_log.h"
#include "android_jni_util.h"
#include <cstdlib>
#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"

namespace apptraverse::android {
using ui::android::CheckedJni;
using ui::android::FailJni;

namespace {
void OnPushButton(JNIEnv*, jclass, jlong presenter) {
  ui::android::NotifyAndroidPushButtonClickedFromJava(presenter);
}
void OnListSelection(JNIEnv*, jclass, jlong presenter, jint row) {
  ui::android::NotifyAndroidListSelectionFromJava(presenter, row);
}
void OnWindowClientSize(JNIEnv*, jclass, jlong presenter,
                        jint width, jint height, jint dpi) {
  ui::android::NotifyAndroidWindowClientSizeFromJava(presenter, width, height, dpi);
}
}  // namespace

bool RegisterAndroidUiNativeMethods(JNIEnv* env) {
  jclass cls = env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  if (cls == nullptr) return false;
  JNINativeMethod const methods[] = {
      {"nativeOnPushButtonClick", "(J)V", reinterpret_cast<void*>(&OnPushButton)},
      {"nativeOnListSelection", "(JI)V", reinterpret_cast<void*>(&OnListSelection)},
      {"nativeOnWindowClientSize", "(JIII)V", reinterpret_cast<void*>(&OnWindowClientSize)},
  };
  jint const result = env->RegisterNatives(cls, methods, 3);
  env->DeleteLocalRef(cls);
  return result == JNI_OK;
}

AndroidUiBridge::AndroidUiBridge(JavaVM* vm, jobject global_object,
                                 jclass global_class,
                                 jmethodID on_publication,
                                 jmethodID on_background_save_finished)
    : vm_{vm},
      object_{global_object},
      class_{global_class},
      on_publication_{on_publication},
      on_background_save_finished_{on_background_save_finished} {}

AndroidUiBridge::~AndroidUiBridge() { Reset(); }

AndroidUiBridge::AndroidUiBridge(AndroidUiBridge&& other) noexcept
    : vm_{other.vm_},
      object_{other.object_},
      class_{other.class_},
      on_publication_{other.on_publication_},
      on_background_save_finished_{other.on_background_save_finished_} {
  other.vm_ = nullptr;
  other.object_ = nullptr;
  other.class_ = nullptr;
}

AndroidUiBridge& AndroidUiBridge::operator=(AndroidUiBridge&& other) noexcept {
  if (this != &other) {
    Reset();
    vm_ = other.vm_;
    object_ = other.object_;
    class_ = other.class_;
    on_publication_ = other.on_publication_;
    on_background_save_finished_ = other.on_background_save_finished_;
    other.vm_ = nullptr;
    other.object_ = nullptr;
    other.class_ = nullptr;
  }
  return *this;
}

void AndroidUiBridge::Reset() {
  if (vm_ == nullptr) {
    object_ = nullptr;
    class_ = nullptr;
    return;
  }
  bool attached_here = false;
  JNIEnv* env = AttachedEnv(attached_here);
  if (env == nullptr) {
    LogError("AndroidUiBridge: failed to attach JNIEnv to release references");
    std::abort();
  }
  {
    if (object_ != nullptr) {
      env->DeleteGlobalRef(object_);
    }
    if (class_ != nullptr) {
      env->DeleteGlobalRef(class_);
    }
  }
  object_ = nullptr;
  class_ = nullptr;
  if (attached_here) vm_->DetachCurrentThread();
  vm_ = nullptr;
}

JNIEnv* AndroidUiBridge::AttachedEnv(bool& attached_here) const {
  attached_here = false;
  if (vm_ == nullptr) {
    return nullptr;
  }
  JNIEnv* env = nullptr;
  jint const status =
      vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (status == JNI_OK) {
    return env;
  }
  if (status == JNI_EDETACHED) {
    if (vm_->AttachCurrentThread(&env, nullptr) != 0) {
      return nullptr;
    }
    attached_here = true;
    return env;
  }
  return nullptr;
}

void AndroidUiBridge::PostBackgroundSaveFinished(int persist_result) const {
  if (on_background_save_finished_ == nullptr) {
    return;
  }
  bool attached_here = false;
  JNIEnv* env = AttachedEnv(attached_here);
  if (env == nullptr) {
    LogError("AndroidUiBridge: failed to attach JNIEnv for background save");
    std::abort();
  }
  env->CallVoidMethod(object_, on_background_save_finished_,
                      static_cast<jint>(persist_result));
  if (env->ExceptionCheck()) {
    FailJni(env, "onNativeBackgroundSaveFinished");
  }
  if (attached_here) vm_->DetachCurrentThread();
}

void AndroidUiBridge::PostPublication(int kind) const {
  bool attached_here = false;
  JNIEnv* env = AttachedEnv(attached_here);
  if (env == nullptr) {
    LogError("AndroidUiBridge: failed to attach JNIEnv for publication");
    std::abort();
  }
  env->CallVoidMethod(object_, on_publication_, static_cast<jint>(kind));
  if (env->ExceptionCheck()) {
    FailJni(env, "onNativePublication");
  }
  if (attached_here) vm_->DetachCurrentThread();
}

AndroidUiBridge MakeAndroidUiBridge(JNIEnv* env, jobject ui_bridge) {
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != 0 || vm == nullptr) {
    LogError("MakeAndroidUiBridge: GetJavaVM failed");
    FailJni(env, "GetJavaVM");
  }

  jclass local_class = CheckedJni(env, "bridge class", [&] { return env->GetObjectClass(ui_bridge); });
  jclass global_class = static_cast<jclass>(env->NewGlobalRef(local_class));
  env->DeleteLocalRef(local_class);
  if (global_class == nullptr) {
    LogError("MakeAndroidUiBridge: bridge class global ref failed");
    FailJni(env, "bridge class global reference");
  }

  jmethodID const on_publication =
      env->GetMethodID(global_class, "onNativePublication", "(I)V");
  if (on_publication == nullptr) {
    env->DeleteGlobalRef(global_class);
    LogError("MakeAndroidUiBridge: onNativePublication missing");
    FailJni(env, "onNativePublication");
  }

  jmethodID const on_background_save_finished = env->GetMethodID(
      global_class, "onNativeBackgroundSaveFinished", "(I)V");
  if (on_background_save_finished == nullptr) {
    env->DeleteGlobalRef(global_class);
    LogError("MakeAndroidUiBridge: onNativeBackgroundSaveFinished missing");
    FailJni(env, "onNativeBackgroundSaveFinished");
  }

  jobject global_object = env->NewGlobalRef(ui_bridge);
  if (global_object == nullptr) {
    env->DeleteGlobalRef(global_class);
    LogError("MakeAndroidUiBridge: bridge instance global ref failed");
    FailJni(env, "bridge instance global reference");
  }

  return AndroidUiBridge{vm, global_object, global_class, on_publication,
                         on_background_save_finished};
}

}  // namespace apptraverse::android
