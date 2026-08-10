#include "ui_bridge.h"

#include <utility>

#include "android_log.h"

namespace apptraverse::android {
namespace {

void DeleteGlobalRef(JNIEnv* env, jobject* object) {
  if (env != nullptr && object != nullptr && *object != nullptr) {
    env->DeleteGlobalRef(*object);
    *object = nullptr;
  }
}

}  // namespace

UiBridge::UiBridge(JavaVM* vm, jobject global_object, jclass global_class,
                   jmethodID on_status, jmethodID on_transcript,
                   jmethodID on_message_committed)
    : vm_{vm},
      object_{global_object},
      class_{global_class},
      on_status_{on_status},
      on_transcript_{on_transcript},
      on_message_committed_{on_message_committed} {}

UiBridge::~UiBridge() { Reset(); }

UiBridge::UiBridge(UiBridge&& other) noexcept
    : vm_{other.vm_},
      object_{other.object_},
      class_{other.class_},
      on_status_{other.on_status_},
      on_transcript_{other.on_transcript_},
      on_message_committed_{other.on_message_committed_} {
  other.vm_ = nullptr;
  other.object_ = nullptr;
  other.class_ = nullptr;
  other.on_status_ = nullptr;
  other.on_transcript_ = nullptr;
  other.on_message_committed_ = nullptr;
}

UiBridge& UiBridge::operator=(UiBridge&& other) noexcept {
  if (this != &other) {
    Reset();
    vm_ = other.vm_;
    object_ = other.object_;
    class_ = other.class_;
    on_status_ = other.on_status_;
    on_transcript_ = other.on_transcript_;
    on_message_committed_ = other.on_message_committed_;
    other.vm_ = nullptr;
    other.object_ = nullptr;
    other.class_ = nullptr;
    other.on_status_ = nullptr;
    other.on_transcript_ = nullptr;
    other.on_message_committed_ = nullptr;
  }
  return *this;
}

void UiBridge::Reset() {
  if (vm_ == nullptr) {
    object_ = nullptr;
    class_ = nullptr;
    return;
  }
  JNIEnv* env = AttachedEnv();
  DeleteGlobalRef(env, &object_);
  if (class_ != nullptr && env != nullptr) {
    env->DeleteGlobalRef(class_);
    class_ = nullptr;
  }
  vm_ = nullptr;
  on_status_ = nullptr;
  on_transcript_ = nullptr;
  on_message_committed_ = nullptr;
}

JNIEnv* UiBridge::AttachedEnv() const {
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

void UiBridge::CallStringMethod(jmethodID method,
                                std::string const& text) const {
  if (!is_valid() || method == nullptr) {
    return;
  }
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    LogError("UiBridge: failed to attach JNIEnv");
    return;
  }
  jstring java_text = env->NewStringUTF(text.c_str());
  if (java_text == nullptr) {
    LogError("UiBridge: NewStringUTF failed");
    return;
  }
  env->CallVoidMethod(object_, method, java_text);
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    LogError("UiBridge: Java callback threw");
  }
  env->DeleteLocalRef(java_text);
}

void UiBridge::PostStatus(std::string const& status) const {
  CallStringMethod(on_status_, status);
}

void UiBridge::PostTranscript(std::string const& transcript) const {
  CallStringMethod(on_transcript_, transcript);
}

void UiBridge::PostMessageCommitted(std::string const& text) const {
  CallStringMethod(on_message_committed_, text);
}

UiBridge MakeUiBridge(JNIEnv* env, jobject ui_bridge) {
  if (env == nullptr || ui_bridge == nullptr) {
    return {};
  }
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != 0 || vm == nullptr) {
    return {};
  }

  jclass local_class = env->GetObjectClass(ui_bridge);
  if (local_class == nullptr) {
    return {};
  }
  jclass global_class =
      static_cast<jclass>(env->NewGlobalRef(local_class));
  env->DeleteLocalRef(local_class);
  if (global_class == nullptr) {
    return {};
  }

  jmethodID on_status =
      env->GetMethodID(global_class, "onNativeStatus", "(Ljava/lang/String;)V");
  jmethodID on_transcript = env->GetMethodID(
      global_class, "onNativeTranscript", "(Ljava/lang/String;)V");
  jmethodID on_message_committed = env->GetMethodID(
      global_class, "onNativeMessageCommitted", "(Ljava/lang/String;)V");
  if (on_status == nullptr || on_transcript == nullptr ||
      on_message_committed == nullptr) {
    env->DeleteGlobalRef(global_class);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
    }
    return {};
  }

  jobject global_object = env->NewGlobalRef(ui_bridge);
  if (global_object == nullptr) {
    env->DeleteGlobalRef(global_class);
    return {};
  }

  return UiBridge{vm, global_object, global_class, on_status, on_transcript,
                  on_message_committed};
}

}  // namespace apptraverse::android
