#include <jni.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "android_log.h"
#include "native_runtime.h"
#include "ui_bridge.h"

namespace apptraverse::android {
namespace {

constexpr char const kNativeRuntimeClass[] =
    "com/apptraverse/singleclientchat/NativeRuntime";
constexpr char const kNativeCreateSignature[] =
    "(Ljava/lang/String;Lcom/apptraverse/singleclientchat/NativeUiBridge;)J";

std::string ToStdString(JNIEnv* env, jstring value) {
  if (value == nullptr) {
    return {};
  }
  auto const* chars = env->GetStringUTFChars(value, nullptr);
  if (chars == nullptr) {
    env->ExceptionClear();
    return {};
  }
  auto result = std::string{chars};
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

NativeRuntime* FromHandle(jlong handle) {
  return reinterpret_cast<NativeRuntime*>(handle);
}

jlong NativeCreate(JNIEnv* env, jclass, jstring files_dir, jobject ui_bridge) {
  auto const files_path = ToStdString(env, files_dir);
  if (files_path.empty()) {
    LogError("filesDir is required to create the native runtime");
    return 0;
  }

  auto bridge = MakeUiBridge(env, ui_bridge);
  if (!bridge.is_valid()) {
    return 0;
  }

  auto state_dir = (std::filesystem::path{files_path} / "state").string();
  auto runtime =
      std::make_unique<NativeRuntime>(std::move(state_dir), std::move(bridge));
  return reinterpret_cast<jlong>(runtime.release());
}

void NativeRun(JNIEnv*, jclass, jlong handle) {
  auto* runtime = FromHandle(handle);
  if (runtime == nullptr) {
    return;
  }
  runtime->Run();
}

void NativeQueueSend(JNIEnv* env, jclass, jlong handle, jstring text) {
  auto* runtime = FromHandle(handle);
  if (runtime == nullptr) {
    return;
  }
  (void)runtime->QueueSend(ToStdString(env, text));
}

void NativeQueueAddPeer(JNIEnv* env, jclass, jlong handle, jstring uid) {
  auto* runtime = FromHandle(handle);
  if (runtime == nullptr) {
    return;
  }
  (void)runtime->QueueAddPeer(ToStdString(env, uid));
}

void NativeQueueWindowChanged(JNIEnv*, jclass, jlong handle, jint width,
                              jint height, jint density_dpi) {
  auto* runtime = FromHandle(handle);
  if (runtime == nullptr) {
    return;
  }
  runtime->QueueWindowChanged(width, height, density_dpi);
}

void NativeStop(JNIEnv*, jclass, jlong handle) {
  auto* runtime = FromHandle(handle);
  if (runtime == nullptr) {
    return;
  }
  runtime->Stop();
}

void NativeDestroy(JNIEnv*, jclass, jlong handle) {
  delete FromHandle(handle);
}

JNINativeMethod const kNativeMethods[] = {
    {"nativeCreate", kNativeCreateSignature,
     reinterpret_cast<void*>(&NativeCreate)},
    {"nativeRun", "(J)V", reinterpret_cast<void*>(&NativeRun)},
    {"nativeQueueSend", "(JLjava/lang/String;)V",
     reinterpret_cast<void*>(&NativeQueueSend)},
    {"nativeQueueAddPeer", "(JLjava/lang/String;)V",
     reinterpret_cast<void*>(&NativeQueueAddPeer)},
    {"nativeQueueWindowChanged", "(JIII)V",
     reinterpret_cast<void*>(&NativeQueueWindowChanged)},
    {"nativeStop", "(J)V", reinterpret_cast<void*>(&NativeStop)},
    {"nativeDestroy", "(J)V", reinterpret_cast<void*>(&NativeDestroy)},
};

}  // namespace
}  // namespace apptraverse::android

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK ||
      env == nullptr) {
    apptraverse::android::LogError("JNI_OnLoad failed to obtain a JNIEnv");
    return JNI_ERR;
  }

  auto* runtime_class =
      env->FindClass(apptraverse::android::kNativeRuntimeClass);
  if (runtime_class == nullptr) {
    env->ExceptionClear();
    apptraverse::android::LogError("JNI_OnLoad failed to find NativeRuntime");
    return JNI_ERR;
  }

  auto const method_count = static_cast<jint>(
      sizeof(apptraverse::android::kNativeMethods) /
      sizeof(apptraverse::android::kNativeMethods[0]));
  auto const result = env->RegisterNatives(
      runtime_class, apptraverse::android::kNativeMethods, method_count);
  env->DeleteLocalRef(runtime_class);
  if (result != JNI_OK) {
    env->ExceptionClear();
    apptraverse::android::LogError("JNI_OnLoad failed to register natives");
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
