#include <jni.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_bridge.h"
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
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();
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

void NativeRequestBackgroundSave(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->RequestBackgroundSave();
}

void NativeMobilePageShown(JNIEnv*, jclass, jlong handle, jlong window_id) {
  FromHandle(handle)->MobilePageShown(static_cast<std::uint32_t>(window_id));
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
    {"nativeRequestBackgroundSave", "(J)V",
     reinterpret_cast<void*>(&NativeRequestBackgroundSave)},
    {"nativeMobilePageShown", "(JJ)V",
     reinterpret_cast<void*>(&NativeMobilePageShown)},
    {"nativeMobileSwipe", "(JI)V", reinterpret_cast<void*>(&NativeMobileSwipe)},
    {"nativeDestroy", "(J)V", reinterpret_cast<void*>(&NativeDestroy)},
};

}  // namespace
}  // namespace apptraverse::example::surfaces

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    apptraverse::android::LogError("JNI_OnLoad failed to obtain a JNIEnv");
    return JNI_ERR;
  }

  auto* runtime_class =
      env->FindClass(apptraverse::example::surfaces::kNativeRuntimeClass);
  if (runtime_class == nullptr) {
    env->ExceptionClear();
    apptraverse::android::LogError("JNI_OnLoad failed to find NativeRuntime");
    return JNI_ERR;
  }

  auto const method_count =
      static_cast<jint>(sizeof(apptraverse::example::surfaces::kNativeMethods) /
                        sizeof(apptraverse::example::surfaces::kNativeMethods[0]));
  auto const result = env->RegisterNatives(
      runtime_class, apptraverse::example::surfaces::kNativeMethods,
      method_count);
  env->DeleteLocalRef(runtime_class);
  if (result != JNI_OK) {
    env->ExceptionClear();
    apptraverse::android::LogError("JNI_OnLoad failed to register natives");
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
