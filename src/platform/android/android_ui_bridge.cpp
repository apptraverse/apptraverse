#include "apptraverse/platform/android/android_ui_bridge.h"

#include "android_log.h"

namespace apptraverse::android {

AndroidUiBridge::AndroidUiBridge(JavaVM* vm, jobject global_object,
                                 jclass global_class,
                                 jmethodID on_publication)
    : vm_{vm},
      object_{global_object},
      class_{global_class},
      on_publication_{on_publication} {}

AndroidUiBridge::~AndroidUiBridge() { Reset(); }

AndroidUiBridge::AndroidUiBridge(AndroidUiBridge&& other) noexcept
    : vm_{other.vm_},
      object_{other.object_},
      class_{other.class_},
      on_publication_{other.on_publication_} {
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
  JNIEnv* env = AttachedEnv();
  if (env != nullptr) {
    if (object_ != nullptr) {
      env->DeleteGlobalRef(object_);
    }
    if (class_ != nullptr) {
      env->DeleteGlobalRef(class_);
    }
  }
  object_ = nullptr;
  class_ = nullptr;
  vm_ = nullptr;
}

JNIEnv* AndroidUiBridge::AttachedEnv() const {
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
    return env;
  }
  return nullptr;
}

void AndroidUiBridge::PostPublication(int kind) const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    LogError("AndroidUiBridge: failed to attach JNIEnv for publication");
    return;
  }
  env->CallVoidMethod(object_, on_publication_, static_cast<jint>(kind));
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LogError("AndroidUiBridge: onNativePublication threw");
  }
}

AndroidUiBridge MakeAndroidUiBridge(JNIEnv* env, jobject ui_bridge) {
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != 0 || vm == nullptr) {
    LogError("MakeAndroidUiBridge: GetJavaVM failed");
    return {};
  }

  jclass local_class = env->GetObjectClass(ui_bridge);
  jclass global_class = static_cast<jclass>(env->NewGlobalRef(local_class));
  env->DeleteLocalRef(local_class);
  if (global_class == nullptr) {
    LogError("MakeAndroidUiBridge: bridge class global ref failed");
    return {};
  }

  jmethodID const on_publication =
      env->GetMethodID(global_class, "onNativePublication", "(I)V");
  if (on_publication == nullptr) {
    env->ExceptionClear();
    env->DeleteGlobalRef(global_class);
    LogError("MakeAndroidUiBridge: onNativePublication missing");
    return {};
  }

  jobject global_object = env->NewGlobalRef(ui_bridge);
  if (global_object == nullptr) {
    env->DeleteGlobalRef(global_class);
    LogError("MakeAndroidUiBridge: bridge instance global ref failed");
    return {};
  }

  return AndroidUiBridge{vm, global_object, global_class, on_publication};
}

}  // namespace apptraverse::android
