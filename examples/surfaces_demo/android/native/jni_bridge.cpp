#include <jni.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_bridge.h"
#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/ui/ui_presenters.h"

#include "android_log.h"
#include "window_catalog_graph_android.h"
#include "window_catalog_native_runtime.h"
#include "window_catalog_ui_bridge.h"

namespace apptraverse::example::surfaces {
namespace {

constexpr char const kNativeRuntimeClass[] =
    "com/apptraverse/windowcatalog/NativeRuntime";

WindowCatalogNativeRuntime* FromHandle(jlong handle) {
  return reinterpret_cast<WindowCatalogNativeRuntime*>(handle);
}

jlong NativeCreate(JNIEnv* env, jclass, jstring files_dir, jstring state_subdir,
                 jobject ui_bridge) {
  EnsureWindowCatalogAndroidGraphRegistration();

  auto wc_bridge = android::MakeWindowCatalogUiBridge(env, ui_bridge);
  auto android_bridge = android::MakeAndroidUiBridge(env, ui_bridge);
  if (!wc_bridge.is_valid() || !android_bridge.is_valid()) {
    return 0;
  }

  auto const* files_chars = env->GetStringUTFChars(files_dir, nullptr);
  auto const* subdir_chars = env->GetStringUTFChars(state_subdir, nullptr);
  auto state_dir = std::filesystem::path{files_chars} / subdir_chars;
  env->ReleaseStringUTFChars(files_dir, files_chars);
  env->ReleaseStringUTFChars(state_subdir, subdir_chars);

  auto* runtime = new WindowCatalogNativeRuntime(
      std::move(state_dir), std::move(wc_bridge), std::move(android_bridge));
  android::LogInfo("WINDOW_CATALOG_NATIVE_RUNTIME_CREATED");
  return reinterpret_cast<jlong>(runtime);
}

void NativeSetPresentationRoot(JNIEnv* env, jclass, jlong handle, jobject activity,
                               jobject content_root) {
  FromHandle(handle)->SetPresentationRoot(env, activity, content_root);
}

void NativeStartModel(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->StartModelThread();
}

void NativeConsumePublication(JNIEnv*, jclass, jlong handle, jint kind) {
  FromHandle(handle)->ConsumePublication(static_cast<int>(kind));
}

void NativeFlushPendingBackgroundSaves(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->FlushPendingBackgroundSaves();
}

void NativeMobileSwipe(JNIEnv*, jclass, jlong handle, jint delta) {
  FromHandle(handle)->MobileSwipe(static_cast<int>(delta));
}

void NativeDestroy(JNIEnv*, jclass, jlong handle) { delete FromHandle(handle); }

JNINativeMethod const kNativeMethods[] = {
    {"nativeCreate",
     "(Ljava/lang/String;Ljava/lang/String;Lcom/apptraverse/windowcatalog/NativeUiBridge;)J",
     reinterpret_cast<void*>(&NativeCreate)},
    {"nativeSetPresentationRoot", "(JLandroid/app/Activity;Landroid/view/ViewGroup;)V",
     reinterpret_cast<void*>(&NativeSetPresentationRoot)},
    {"nativeStartModel", "(J)V", reinterpret_cast<void*>(&NativeStartModel)},
    {"nativeConsumePublication", "(JI)V",
     reinterpret_cast<void*>(&NativeConsumePublication)},
    {"nativeFlushPendingBackgroundSaves", "(J)V",
     reinterpret_cast<void*>(&NativeFlushPendingBackgroundSaves)},
    {"nativeMobileSwipe", "(JI)V", reinterpret_cast<void*>(&NativeMobileSwipe)},
    {"nativeDestroy", "(J)V", reinterpret_cast<void*>(&NativeDestroy)},
};

bool RegisterNatives(JNIEnv* env, char const* class_name,
                     JNINativeMethod const* methods, jint count) {
  jclass local = env->FindClass(class_name);
  if (local == nullptr) {
    env->ExceptionClear();
    apptraverse::android::LogError("JNI_OnLoad: FindClass failed");
    return false;
  }
  jint const rc = env->RegisterNatives(local, methods, count);
  env->DeleteLocalRef(local);
  if (rc != JNI_OK) {
    env->ExceptionClear();
    return false;
  }
  return true;
}

}  // namespace
}  // namespace apptraverse::example::surfaces

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    apptraverse::android::LogError("JNI_OnLoad failed to obtain a JNIEnv");
    return JNI_ERR;
  }

  using apptraverse::example::surfaces::kNativeMethods;
  using apptraverse::example::surfaces::kNativeRuntimeClass;
  using apptraverse::example::surfaces::RegisterNatives;

  if (!RegisterNatives(
          env, kNativeRuntimeClass, kNativeMethods,
          static_cast<jint>(sizeof(kNativeMethods) / sizeof(kNativeMethods[0])))) {
    apptraverse::android::LogError(
        "JNI_OnLoad: NativeRuntime RegisterNatives failed");
    return JNI_ERR;
  }
  if (!apptraverse::android::RegisterAndroidUiNativeMethods(env)) {
    apptraverse::android::LogError(
        "JNI_OnLoad: AppTraverseAndroidUi RegisterNatives failed");
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
