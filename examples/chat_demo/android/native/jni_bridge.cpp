#include <jni.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "apptraverse/object_macros.h"
#include "chat_model.h"

using apptraverse::example::chat_demo::ScrollAnchor;

#include "android_log.h"
#include "chat_native_runtime.h"
#include "chat_ui_bridge.h"

namespace apptraverse::example::chat_demo::android {
namespace {

constexpr char const kNativeRuntimeClass[] = "com/apptraverse/chatdemo/NativeRuntime";
constexpr char const kNativeCreateSignature[] =
    "(Ljava/lang/String;Lcom/apptraverse/chatdemo/NativeUiBridge;)J";

ChatNativeRuntime* FromHandle(jlong handle) {
  return reinterpret_cast<ChatNativeRuntime*>(handle);
}

std::string JbytesToUtf8(JNIEnv* env, jbyteArray arr) {
  if (arr == nullptr) {
    return "";
  }
  jsize const len = env->GetArrayLength(arr);
  if (len <= 0) {
    return "";
  }
  jbyte* bytes = env->GetByteArrayElements(arr, nullptr);
  std::string out(reinterpret_cast<char*>(bytes), static_cast<std::size_t>(len));
  env->ReleaseByteArrayElements(arr, bytes, JNI_ABORT);
  return out;
}

std::optional<std::string> OptionalJbytesToUtf8(JNIEnv* env, jbyteArray arr) {
  if (arr == nullptr) {
    return std::nullopt;
  }
  return JbytesToUtf8(env, arr);
}

jlong NativeCreate(JNIEnv* env, jclass, jstring files_dir, jobject ui_bridge) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();

  char const* chars = env->GetStringUTFChars(files_dir, nullptr);
  std::filesystem::path const files_path{chars};
  env->ReleaseStringUTFChars(files_dir, chars);

  auto bridge = MakeChatUiBridge(env, ui_bridge);
  if (!bridge.is_valid()) {
    return 0;
  }

  auto state_dir = files_path / "chat-profile";
  std::filesystem::create_directories(state_dir);
  auto runtime = std::make_unique<ChatNativeRuntime>(std::move(state_dir), std::move(bridge));
  LogMarker("CHAT_NATIVE_RUNTIME_CREATED");
  return reinterpret_cast<jlong>(runtime.release());
}

void NativeStart(JNIEnv*, jclass, jlong handle) { FromHandle(handle)->Start(); }

void NativeOpenPeer(JNIEnv* env, jclass, jlong handle, jbyteArray admin_id,
                    jbyteArray peer_uid) {
  FromHandle(handle)->OpenPeer(JbytesToUtf8(env, admin_id),
                               OptionalJbytesToUtf8(env, peer_uid));
}

void NativeSelectChat(JNIEnv*, jclass, jlong handle, jlong entry_id) {
  FromHandle(handle)->SelectChat(ae::ObjId{static_cast<std::uint32_t>(entry_id)});
}

void NativeEditDraft(JNIEnv* env, jclass, jlong handle, jlong entry_id, jbyteArray text,
                     jlong edit_revision) {
  FromHandle(handle)->EditDraft(ae::ObjId{static_cast<std::uint32_t>(entry_id)},
                                JbytesToUtf8(env, text),
                                static_cast<std::uint64_t>(edit_revision));
}

void NativeSendDraft(JNIEnv* env, jclass, jlong handle, jlong entry_id, jbyteArray text,
                     jlong edit_revision) {
  FromHandle(handle)->SendDraft(ae::ObjId{static_cast<std::uint32_t>(entry_id)},
                                JbytesToUtf8(env, text),
                                static_cast<std::uint64_t>(edit_revision));
}

void NativeSaveScroll(JNIEnv* env, jclass, jlong handle, jlong entry_id, jboolean follow_tail,
                      jbyteArray msg_uid, jlong msg_seq, jdouble offset) {
  ScrollAnchor anchor{
      .follow_tail = follow_tail == JNI_TRUE,
      .first_visible_message =
          {
              .origin_uid = JbytesToUtf8(env, msg_uid),
              .origin_sequence = static_cast<std::uint64_t>(msg_seq),
          },
      .offset_from_message_top = offset,
  };
  FromHandle(handle)->SaveScroll(ae::ObjId{static_cast<std::uint32_t>(entry_id)}, anchor);
}

void NativeCheckpoint(JNIEnv*, jclass, jlong handle) { FromHandle(handle)->Checkpoint(); }

void NativeRequestStop(JNIEnv*, jclass, jlong handle) { FromHandle(handle)->RequestStop(); }

jboolean NativeIsStopped(JNIEnv*, jclass, jlong handle) {
  return FromHandle(handle)->IsStopped() ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeConsumeUiUpdate(JNIEnv*, jclass, jlong handle) {
  return FromHandle(handle)->ConsumeUiUpdate() ? JNI_TRUE : JNI_FALSE;
}

void NativeSetWideLayout(JNIEnv*, jclass, jlong handle, jboolean wide) {
  FromHandle(handle)->SetWideLayout(wide == JNI_TRUE);
}

void NativeDestroy(JNIEnv*, jclass, jlong handle) { delete FromHandle(handle); }

JNINativeMethod const kNativeMethods[] = {
    {"nativeCreate", kNativeCreateSignature, reinterpret_cast<void*>(&NativeCreate)},
    {"nativeStart", "(J)V", reinterpret_cast<void*>(&NativeStart)},
    {"nativeOpenPeer", "(J[B[B)V", reinterpret_cast<void*>(&NativeOpenPeer)},
    {"nativeSelectChat", "(JJ)V", reinterpret_cast<void*>(&NativeSelectChat)},
    {"nativeEditDraft", "(JJ[BJ)V", reinterpret_cast<void*>(&NativeEditDraft)},
    {"nativeSendDraft", "(JJ[BJ)V", reinterpret_cast<void*>(&NativeSendDraft)},
    {"nativeSaveScroll", "(JJZ[BJD)V", reinterpret_cast<void*>(&NativeSaveScroll)},
    {"nativeCheckpoint", "(J)V", reinterpret_cast<void*>(&NativeCheckpoint)},
    {"nativeRequestStop", "(J)V", reinterpret_cast<void*>(&NativeRequestStop)},
    {"nativeIsStopped", "(J)Z", reinterpret_cast<void*>(&NativeIsStopped)},
    {"nativeConsumeUiUpdate", "(J)Z", reinterpret_cast<void*>(&NativeConsumeUiUpdate)},
    {"nativeSetWideLayout", "(JZ)V", reinterpret_cast<void*>(&NativeSetWideLayout)},
    {"nativeDestroy", "(J)V", reinterpret_cast<void*>(&NativeDestroy)},
};

}  // namespace
}  // namespace apptraverse::example::chat_demo::android

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    apptraverse::example::chat_demo::android::LogError("JNI_OnLoad failed to obtain JNIEnv");
    return JNI_ERR;
  }

  jclass runtime_class = env->FindClass(apptraverse::example::chat_demo::android::kNativeRuntimeClass);
  if (runtime_class == nullptr) {
    env->ExceptionClear();
    apptraverse::example::chat_demo::android::LogError("JNI_OnLoad failed to find NativeRuntime");
    return JNI_ERR;
  }

  auto const method_count = static_cast<jint>(sizeof(apptraverse::example::chat_demo::android::kNativeMethods) /
                                              sizeof(apptraverse::example::chat_demo::android::kNativeMethods[0]));
  auto const result = env->RegisterNatives(runtime_class, apptraverse::example::chat_demo::android::kNativeMethods,
                                           method_count);
  env->DeleteLocalRef(runtime_class);
  if (result != JNI_OK) {
    env->ExceptionClear();
    apptraverse::example::chat_demo::android::LogError("JNI_OnLoad failed to register natives");
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
