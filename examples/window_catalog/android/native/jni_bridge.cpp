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
#include "android_jni_util.h"
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
                 jstring local_seed_subdir, jobject ui_bridge) {
  EnsureWindowCatalogAndroidGraphRegistration();

  auto wc_bridge = android::MakeWindowCatalogUiBridge(env, ui_bridge);
  auto android_bridge = android::MakeAndroidUiBridge(env, ui_bridge);
  if (!wc_bridge.is_valid() || !android_bridge.is_valid()) {
    return 0;
  }

  auto state_dir = std::filesystem::path{ui::android::JstringToUtf8(env, files_dir)} /
                   ui::android::JstringToUtf8(env, state_subdir);

  std::filesystem::path local_seed_dir;
  if (local_seed_subdir != nullptr)
    local_seed_dir = std::filesystem::path{ui::android::JstringToUtf8(env, files_dir)} /
                     ui::android::JstringToUtf8(env, local_seed_subdir);
  auto* runtime = new WindowCatalogNativeRuntime(
      std::move(state_dir), std::move(local_seed_dir), std::move(wc_bridge), std::move(android_bridge));
  return reinterpret_cast<jlong>(runtime);
}

void NativeSetPresentationRoot(JNIEnv* env, jclass, jlong handle, jobject activity,
                               jobject content_root) {
  FromHandle(handle)->SetPresentationRoot(env, activity, content_root);
}

void NativeStartModel(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->StartModelThread();
}

void NativeClearPresentationRoot(JNIEnv* env, jclass, jlong handle, jobject activity) {
  FromHandle(handle)->ClearPresentationRoot(env, activity);
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

void NativePopulateMenu(JNIEnv* env, jclass, jlong handle, jobject menu) {
  FromHandle(handle)->PopulateMenu(env, menu);
}

void NativeExecuteMenuCommand(JNIEnv*, jclass, jlong handle, jint native_item_id) {
  FromHandle(handle)->ExecuteMenuCommand(native_item_id);
}
void NativeExitFullscreen(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->ExitFullscreen();
}

void NativeDestroy(JNIEnv*, jclass, jlong handle) { delete FromHandle(handle); }

JNINativeMethod const kNativeMethods[] = {
    {"nativeCreate",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lcom/apptraverse/windowcatalog/NativeUiBridge;)J",
     reinterpret_cast<void*>(&NativeCreate)},
    {"nativeSetPresentationRoot", "(JLandroid/app/Activity;Landroid/view/ViewGroup;)V",
     reinterpret_cast<void*>(&NativeSetPresentationRoot)},
    {"nativeStartModel", "(J)V", reinterpret_cast<void*>(&NativeStartModel)},
    {"nativeClearPresentationRoot", "(JLandroid/app/Activity;)V",
     reinterpret_cast<void*>(&NativeClearPresentationRoot)},
    {"nativeConsumePublication", "(JI)V",
     reinterpret_cast<void*>(&NativeConsumePublication)},
    {"nativeFlushPendingBackgroundSaves", "(J)V",
     reinterpret_cast<void*>(&NativeFlushPendingBackgroundSaves)},
    {"nativeMobileSwipe", "(JI)V", reinterpret_cast<void*>(&NativeMobileSwipe)},
    {"nativePopulateMenu", "(JLandroid/view/Menu;)V", reinterpret_cast<void*>(&NativePopulateMenu)},
    {"nativeExecuteMenuCommand", "(JI)V", reinterpret_cast<void*>(&NativeExecuteMenuCommand)},
    {"nativeExitFullscreen", "(J)V", reinterpret_cast<void*>(&NativeExitFullscreen)},
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
