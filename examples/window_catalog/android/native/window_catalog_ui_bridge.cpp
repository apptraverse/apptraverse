#include "window_catalog_ui_bridge.h"

#include <utility>

#include "android_log.h"
#include "android_jni_util.h"
#include <cstdlib>

namespace apptraverse::android {
using ui::android::CheckedJni;
using ui::android::FailJni;

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
  bool attached_here = false;
  JNIEnv* env = AttachedEnv(attached_here);
  if (env == nullptr) { LogError("WindowCatalog JNI attach failed"); std::abort(); }
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

JNIEnv* WindowCatalogUiBridge::AttachedEnv(bool& attached_here) const {
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

void WindowCatalogUiBridge::PostPublication(int kind) const {
  bool attached_here = false;
  JNIEnv* env = AttachedEnv(attached_here);
  if (env == nullptr) { LogError("WindowCatalog JNI attach failed"); std::abort(); }
  env->CallVoidMethod(object_, on_publication_, static_cast<jint>(kind));
  if (env->ExceptionCheck()) {
    FailJni(env, "WindowCatalog publication");
  }
  if (attached_here) vm_->DetachCurrentThread();
}

void WindowCatalogUiBridge::PostMobilePages(
    std::vector<std::int64_t> const& window_ids,
    std::int64_t active_id) const {
  bool attached_here = false;
  JNIEnv* env = AttachedEnv(attached_here);
  if (env == nullptr) { LogError("WindowCatalog JNI attach failed"); std::abort(); }
  jlongArray ids = CheckedJni(env, "page IDs", [&] { return env->NewLongArray(static_cast<jsize>(window_ids.size())); });
  if (!window_ids.empty()) {
    env->SetLongArrayRegion(ids, 0, static_cast<jsize>(window_ids.size()),
                            window_ids.data());
  }
  env->CallVoidMethod(object_, on_mobile_pages_, ids,
                      static_cast<jlong>(active_id));
  if (ids != nullptr) {
    env->DeleteLocalRef(ids);
  }
  if (env->ExceptionCheck()) {
    FailJni(env, "WindowCatalog mobile pages");
  }
  if (attached_here) vm_->DetachCurrentThread();
}

void WindowCatalogUiBridge::PostBackgroundSaveFinished(
    int persist_result) const {
  bool attached_here = false;
  JNIEnv* env = AttachedEnv(attached_here);
  if (env == nullptr) { LogError("WindowCatalog JNI attach failed"); std::abort(); }
  env->CallVoidMethod(object_, on_background_save_finished_,
                      static_cast<jint>(persist_result));
  if (env->ExceptionCheck()) {
    FailJni(env, "WindowCatalog save callback");
  }
  if (attached_here) vm_->DetachCurrentThread();
}

WindowCatalogUiBridge MakeWindowCatalogUiBridge(JNIEnv* env,
                                                jobject ui_bridge) {
  if (ui_bridge == nullptr) {
    FailJni(env, "WindowCatalog bridge creation");
  }
  JavaVM* vm = nullptr;
  if (CheckedJni(env, "GetJavaVM", [&] { return env->GetJavaVM(&vm); }) != JNI_OK || vm == nullptr) {
    FailJni(env, "WindowCatalog bridge creation");
  }
  jclass local_class = CheckedJni(env, "GetObjectClass", [&] { return env->GetObjectClass(ui_bridge); });
  if (local_class == nullptr) {
    FailJni(env, "WindowCatalog bridge creation");
  }
  jmethodID on_publication = CheckedJni(env, "GetMethodID", [&] { return env->GetMethodID(local_class, "onPublication", "(I)V"); });
  jmethodID on_mobile_pages =
      CheckedJni(env, "GetMethodID", [&] { return env->GetMethodID(local_class, "onMobilePages", "([JJ)V"); });
  jmethodID on_background_save_finished = CheckedJni(env, "GetMethodID", [&] { return env->GetMethodID(
      local_class, "onBackgroundSaveFinished", "(I)V"); });
  if (on_publication == nullptr || on_mobile_pages == nullptr ||
      on_background_save_finished == nullptr) {
    CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(local_class); });
    FailJni(env, "WindowCatalog bridge creation");
  }
  jobject global_object = CheckedJni(env, "NewGlobalRef", [&] { return env->NewGlobalRef(ui_bridge); });
  jclass global_class = static_cast<jclass>(CheckedJni(env, "NewGlobalRef", [&] { return env->NewGlobalRef(local_class); }));
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(local_class); });
  return WindowCatalogUiBridge{vm, global_object, global_class, on_publication,
                               on_mobile_pages, on_background_save_finished};
}

}  // namespace apptraverse::android
