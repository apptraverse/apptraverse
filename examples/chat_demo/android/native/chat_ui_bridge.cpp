#include "chat_ui_bridge.h"

#include <utility>

#include "android_log.h"

namespace apptraverse::example::chat_demo::android {
namespace {

jbyteArray ToJbytes(JNIEnv* env, std::string const& text) {
  auto const len = static_cast<jsize>(text.size());
  jbyteArray arr = env->NewByteArray(len);
  if (len > 0) {
    env->SetByteArrayRegion(arr, 0, len, reinterpret_cast<jbyte const*>(text.data()));
  }
  return arr;
}

}  // namespace

ChatUiBridge::ChatUiBridge(JavaVM* vm, jobject global_object, jclass global_class,
                           jmethodID on_notify, jmethodID on_state, jmethodID on_stopped)
    : vm_{vm},
      object_{global_object},
      class_{global_class},
      on_notify_{on_notify},
      on_state_{on_state},
      on_stopped_{on_stopped} {}

ChatUiBridge::~ChatUiBridge() { Reset(); }

ChatUiBridge::ChatUiBridge(ChatUiBridge&& other) noexcept
    : vm_{other.vm_},
      object_{other.object_},
      class_{other.class_},
      on_notify_{other.on_notify_},
      on_state_{other.on_state_},
      on_stopped_{other.on_stopped_} {
  other.vm_ = nullptr;
  other.object_ = nullptr;
  other.class_ = nullptr;
}

ChatUiBridge& ChatUiBridge::operator=(ChatUiBridge&& other) noexcept {
  if (this != &other) {
    Reset();
    vm_ = other.vm_;
    object_ = other.object_;
    class_ = other.class_;
    on_notify_ = other.on_notify_;
    on_state_ = other.on_state_;
    on_stopped_ = other.on_stopped_;
    other.vm_ = nullptr;
    other.object_ = nullptr;
    other.class_ = nullptr;
  }
  return *this;
}

void ChatUiBridge::Reset() {
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

JNIEnv* ChatUiBridge::AttachedEnv() const {
  if (vm_ == nullptr) {
    return nullptr;
  }
  JNIEnv* env = nullptr;
  jint const status = vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
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

void ChatUiBridge::PostNotify() const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    LogError("ChatUiBridge: attach failed for notify");
    return;
  }
  env->CallVoidMethod(object_, on_notify_);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    LogError("ChatUiBridge: onNativeNotify threw");
  }
}

void ChatUiBridge::PostState(long selected_entry_id, std::vector<long> const& entry_ids,
                             std::vector<std::string> const& entry_labels,
                             std::string const& draft, std::string const& transcript,
                             std::string const& status, std::string const& presence,
                             bool follow_tail, std::string const& scroll_msg_uid,
                             std::uint64_t scroll_msg_seq, double scroll_offset,
                             bool send_enabled, bool wide_layout) const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    LogError("ChatUiBridge: attach failed for state");
    return;
  }

  auto const count = static_cast<jsize>(entry_ids.size());
  jlongArray ids = env->NewLongArray(count);
  env->SetLongArrayRegion(ids, 0, count, entry_ids.data());

  jclass byte_array_class = env->FindClass("[B");
  jobjectArray labels = env->NewObjectArray(count, byte_array_class, nullptr);
  for (jsize i = 0; i < count; ++i) {
    jbyteArray label = ToJbytes(env, entry_labels[static_cast<std::size_t>(i)]);
    env->SetObjectArrayElement(labels, i, label);
    env->DeleteLocalRef(label);
  }

  jbyteArray draft_bytes = ToJbytes(env, draft);
  jbyteArray transcript_bytes = ToJbytes(env, transcript);
  jbyteArray status_bytes = ToJbytes(env, status);
  jbyteArray presence_bytes = ToJbytes(env, presence);
  jbyteArray scroll_uid_bytes = ToJbytes(env, scroll_msg_uid);

  env->CallVoidMethod(
      object_, on_state_, static_cast<jlong>(selected_entry_id), ids, labels, draft_bytes,
      transcript_bytes, status_bytes, presence_bytes,
      follow_tail ? JNI_TRUE : JNI_FALSE, scroll_uid_bytes,
      static_cast<jlong>(scroll_msg_seq), static_cast<jdouble>(scroll_offset),
      send_enabled ? JNI_TRUE : JNI_FALSE, wide_layout ? JNI_TRUE : JNI_FALSE);

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    LogError("ChatUiBridge: onNativeState threw");
  }

  env->DeleteLocalRef(ids);
  env->DeleteLocalRef(labels);
  env->DeleteLocalRef(draft_bytes);
  env->DeleteLocalRef(transcript_bytes);
  env->DeleteLocalRef(status_bytes);
  env->DeleteLocalRef(presence_bytes);
  env->DeleteLocalRef(scroll_uid_bytes);
  env->DeleteLocalRef(byte_array_class);
}

void ChatUiBridge::PostStopped() const {
  JNIEnv* env = AttachedEnv();
  if (env == nullptr) {
    LogError("ChatUiBridge: attach failed for stop");
    return;
  }
  env->CallVoidMethod(object_, on_stopped_);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    LogError("ChatUiBridge: onNativeStopped threw");
  }
}

ChatUiBridge MakeChatUiBridge(JNIEnv* env, jobject ui_bridge) {
  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != 0 || vm == nullptr) {
    LogError("MakeChatUiBridge: GetJavaVM failed");
    return {};
  }

  jclass local_class = env->GetObjectClass(ui_bridge);
  jclass global_class = static_cast<jclass>(env->NewGlobalRef(local_class));
  env->DeleteLocalRef(local_class);
  if (global_class == nullptr) {
    return {};
  }

  jmethodID const on_notify = env->GetMethodID(global_class, "onNativeNotify", "()V");
  jmethodID const on_state =
      env->GetMethodID(global_class, "onNativeState",
                       "(J[J[[B[B[B[B[BZJ[DZZ)V");
  jmethodID const on_stopped = env->GetMethodID(global_class, "onNativeStopped", "()V");
  if (on_notify == nullptr || on_state == nullptr || on_stopped == nullptr) {
    env->ExceptionClear();
    env->DeleteGlobalRef(global_class);
    LogError("MakeChatUiBridge: bridge methods do not match");
    return {};
  }

  jobject global_object = env->NewGlobalRef(ui_bridge);
  if (global_object == nullptr) {
    env->DeleteGlobalRef(global_class);
    return {};
  }

  return ChatUiBridge{vm, global_object, global_class, on_notify, on_state, on_stopped};
}

}  // namespace apptraverse::example::chat_demo::android
