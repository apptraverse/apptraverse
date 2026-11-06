#include "window_catalog_ui_bridge.h"

#include <utility>

#include "android_log.h"

namespace apptraverse::android {

WindowCatalogUiBridge::WindowCatalogUiBridge(
    JavaVM* vm, jobject global_object, jclass global_class,
    jmethodID on_publication, jmethodID on_mobile_pages,
    jmethodID on_background_save_finished)
    : vm_{vm},
      object_{global_object},
      class_{global_class},
      on_publication_{on_publication},
      on_mobile_pages_{on_mobile_pages},
      on_background_save_finished_{on_background_save_finished} {}

WindowCatalogUiBridge::~WindowCatalogUiBridge() { Reset(); }

WindowCatalogUiBridge::WindowCatalogUiBridge(
    WindowCatalogUiBridge&& other) noexcept
    : vm_{other.vm_},
      object_{other.object_},
      class_{other.class_},
      on_publication_{other.on_publication_},
      on_mobile_pages_{other.on_mobile_pages_},
      on_background_save_finished_{other.on_background_save_finished_} {
  other.vm_ = nullptr;
  other.object_ = nullptr;
  other.class_ = nullptr;
}

WindowCatalogUiBridge& WindowCatalogUiBridge::operator=(
    WindowCatalogUiBridge&& other) noexcept {
  if (this != &other) {
    Reset();
    vm_ = other.vm_;
    object_ = other.object_;
    class_ = other.class_;
    on_publication_ = other.on_publication_;
    on_mobile_pages_ = other.on_mobile_pages_;
    on_background_save_finished_ = other.on_background_save_finished_;
    other.vm_ = nullptr;
    other.object_ = nullptr;
    other.class_ = nullptr;
  }
  return *this;
}

void WindowCatalogUiBridge::Reset() {
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

JNIEnv* WindowCatalogUiBridge::AttachedEnv() const {
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

void WindowCatalogUiBridge::PostPublication(int kind) const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    return;
  }
  env->CallVoidMethod(object_, on_publication_, static_cast<jint>(kind));
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LogError("WindowCatalogUiBridge publication callback failed");
  }
}

void WindowCatalogUiBridge::PostMobilePages(
    std::vector<std::int64_t> const& window_ids,
    std::int64_t active_id) const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    return;
  }
  jlongArray ids = env->NewLongArray(static_cast<jsize>(window_ids.size()));
  if (ids != nullptr && !window_ids.empty()) {
    env->SetLongArrayRegion(ids, 0, static_cast<jsize>(window_ids.size()),
                            window_ids.data());
  }
  env->CallVoidMethod(object_, on_mobile_pages_, ids,
                      static_cast<jlong>(active_id));
  if (ids != nullptr) {
    env->DeleteLocalRef(ids);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LogError("WindowCatalogUiBridge mobile pages callback failed");
  }
}

void WindowCatalogUiBridge::PostBackgroundSaveFinished(
    int persist_result) const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    return;
  }
  env->CallVoidMethod(object_, on_background_save_finished_,
                      static_cast<jint>(persist_result));
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
}

WindowCatalogUiBridge MakeWindowCatalogUiBridge(JNIEnv* env,
                                                jobject ui_bridge) {
  if (ui_bridge == nullptr) {
    return {};
  }
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != JNI_OK || vm == nullptr) {
    return {};
  }
  jclass local_class = env->GetObjectClass(ui_bridge);
  if (local_class == nullptr) {
    return {};
  }
  jmethodID on_publication = env->GetMethodID(local_class, "onPublication", "(I)V");
  jmethodID on_mobile_pages =
      env->GetMethodID(local_class, "onMobilePages", "([JJ)V");
  jmethodID on_background_save_finished = env->GetMethodID(
      local_class, "onBackgroundSaveFinished", "(I)V");
  if (on_publication == nullptr || on_mobile_pages == nullptr ||
      on_background_save_finished == nullptr) {
    env->DeleteLocalRef(local_class);
    return {};
  }
  jobject global_object = env->NewGlobalRef(ui_bridge);
  jclass global_class = static_cast<jclass>(env->NewGlobalRef(local_class));
  env->DeleteLocalRef(local_class);
  return WindowCatalogUiBridge{vm, global_object, global_class, on_publication,
                               on_mobile_pages, on_background_save_finished};
}

}  // namespace apptraverse::android
