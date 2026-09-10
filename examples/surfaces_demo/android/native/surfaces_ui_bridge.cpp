#include "surfaces_ui_bridge.h"

#include <utility>

#include "android_log.h"

namespace apptraverse::android {

SurfacesUiBridge::SurfacesUiBridge(JavaVM* vm, jobject global_object,
                                   jclass global_class,
                                   jmethodID on_publication,
                                   jmethodID on_pages, jmethodID on_stopped)
    : vm_{vm},
      object_{global_object},
      class_{global_class},
      on_publication_{on_publication},
      on_pages_{on_pages},
      on_stopped_{on_stopped} {}

SurfacesUiBridge::~SurfacesUiBridge() { Reset(); }

SurfacesUiBridge::SurfacesUiBridge(SurfacesUiBridge&& other) noexcept
    : vm_{other.vm_},
      object_{other.object_},
      class_{other.class_},
      on_publication_{other.on_publication_},
      on_pages_{other.on_pages_},
      on_stopped_{other.on_stopped_} {
  other.vm_ = nullptr;
  other.object_ = nullptr;
  other.class_ = nullptr;
}

SurfacesUiBridge& SurfacesUiBridge::operator=(
    SurfacesUiBridge&& other) noexcept {
  if (this != &other) {
    Reset();
    vm_ = other.vm_;
    object_ = other.object_;
    class_ = other.class_;
    on_publication_ = other.on_publication_;
    on_pages_ = other.on_pages_;
    on_stopped_ = other.on_stopped_;
    other.vm_ = nullptr;
    other.object_ = nullptr;
    other.class_ = nullptr;
  }
  return *this;
}

void SurfacesUiBridge::Reset() {
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

JNIEnv* SurfacesUiBridge::AttachedEnv() const {
  if (vm_ == nullptr) {
    return nullptr;
  }
  JNIEnv* env = nullptr;
  jint const status =
      vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (status == JNI_OK) {
    return env;
  }
  // The model thread is a Java thread here, but attach anyway so a future
  // native-only thread still reaches the bridge.
  if (status == JNI_EDETACHED) {
    if (vm_->AttachCurrentThread(&env, nullptr) != 0) {
      return nullptr;
    }
    return env;
  }
  return nullptr;
}

void SurfacesUiBridge::PostPublication(int kind) const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    LogError("SurfacesUiBridge: failed to attach JNIEnv for publication");
    return;
  }
  env->CallVoidMethod(object_, on_publication_, static_cast<jint>(kind));
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LogError("SurfacesUiBridge: onNativePublication threw");
  }
}

void SurfacesUiBridge::PostPages(
    std::vector<std::int64_t> const& ids,
    std::vector<std::int32_t> const& numbers) const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    LogError("SurfacesUiBridge: failed to attach JNIEnv for pages");
    return;
  }
  auto const count = static_cast<jsize>(ids.size());
  jlongArray java_ids = env->NewLongArray(count);
  jintArray java_numbers = env->NewIntArray(count);
  if (java_ids == nullptr || java_numbers == nullptr) {
    LogError("SurfacesUiBridge: page array allocation failed");
    return;
  }
  env->SetLongArrayRegion(java_ids, 0, count,
                          reinterpret_cast<jlong const*>(ids.data()));
  env->SetIntArrayRegion(java_numbers, 0, count,
                         reinterpret_cast<jint const*>(numbers.data()));
  env->CallVoidMethod(object_, on_pages_, java_ids, java_numbers);
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LogError("SurfacesUiBridge: onNativePages threw");
  }
  env->DeleteLocalRef(java_ids);
  env->DeleteLocalRef(java_numbers);
}

void SurfacesUiBridge::PostStopped() const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    LogError("SurfacesUiBridge: failed to attach JNIEnv for stop");
    return;
  }
  env->CallVoidMethod(object_, on_stopped_);
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LogError("SurfacesUiBridge: onNativeStopped threw");
  }
}

SurfacesUiBridge MakeSurfacesUiBridge(JNIEnv* env, jobject ui_bridge) {
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != 0 || vm == nullptr) {
    LogError("MakeSurfacesUiBridge: GetJavaVM failed");
    return {};
  }

  jclass local_class = env->GetObjectClass(ui_bridge);
  jclass global_class = static_cast<jclass>(env->NewGlobalRef(local_class));
  env->DeleteLocalRef(local_class);
  if (global_class == nullptr) {
    LogError("MakeSurfacesUiBridge: bridge class global ref failed");
    return {};
  }

  jmethodID const on_publication =
      env->GetMethodID(global_class, "onNativePublication", "(I)V");
  jmethodID const on_pages =
      env->GetMethodID(global_class, "onNativePages", "([J[I)V");
  jmethodID const on_stopped =
      env->GetMethodID(global_class, "onNativeStopped", "()V");
  if (on_publication == nullptr || on_pages == nullptr ||
      on_stopped == nullptr) {
    env->ExceptionClear();
    env->DeleteGlobalRef(global_class);
    LogError("MakeSurfacesUiBridge: bridge methods do not match");
    return {};
  }

  jobject global_object = env->NewGlobalRef(ui_bridge);
  if (global_object == nullptr) {
    env->DeleteGlobalRef(global_class);
    LogError("MakeSurfacesUiBridge: bridge instance global ref failed");
    return {};
  }

  return SurfacesUiBridge{vm,       global_object, global_class,
                          on_publication, on_pages,      on_stopped};
}

}  // namespace apptraverse::android
