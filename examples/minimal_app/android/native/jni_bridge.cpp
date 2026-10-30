#include <jni.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_bridge.h"
#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"

#include "android_log.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_application.h"
#include "minimal_native_runtime.h"

namespace apptraverse::example::minimal {
namespace {

constexpr char const kNativeRuntimeClass[] =
    "com/apptraverse/minimal/NativeRuntime";

MinimalNativeRuntime* FromHandle(jlong handle) {
  return reinterpret_cast<MinimalNativeRuntime*>(handle);
}

jlong NativeCreate(JNIEnv* env, jclass, jstring files_dir, jobject ui_bridge) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

  auto bridge = android::MakeAndroidUiBridge(env, ui_bridge);
  if (!bridge.is_valid()) {
    return 0;
  }

  char const* chars = env->GetStringUTFChars(files_dir, nullptr);
  auto state_dir =
      std::filesystem::path{chars} / "minimal_app_android_state";
  env->ReleaseStringUTFChars(files_dir, chars);

  auto* runtime = new MinimalNativeRuntime(std::move(state_dir), std::move(bridge));
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

void NativeDestroy(JNIEnv*, jclass, jlong handle) { delete FromHandle(handle); }

JNINativeMethod const kNativeMethods[] = {
    {"nativeCreate", "(Ljava/lang/String;Lcom/apptraverse/minimal/NativeUiBridge;)J",
     reinterpret_cast<void*>(&NativeCreate)},
    {"nativeSetPresentationRoot", "(JLandroid/app/Activity;Landroid/view/ViewGroup;)V",
     reinterpret_cast<void*>(&NativeSetPresentationRoot)},
    {"nativeStartModel", "(J)V", reinterpret_cast<void*>(&NativeStartModel)},
    {"nativeConsumePublication", "(JI)V",
     reinterpret_cast<void*>(&NativeConsumePublication)},
    {"nativeDestroy", "(J)V", reinterpret_cast<void*>(&NativeDestroy)},
};

constexpr char const kAndroidUiClass[] =
    "com/apptraverse/ui/android/AppTraverseAndroidUi";

void NativeOnPushButtonClick(JNIEnv*, jclass, jlong native_presenter) {
  ui::android::NotifyAndroidPushButtonClickedFromJava(native_presenter);
}

void NativeOnListSelection(JNIEnv*, jclass, jlong native_presenter, jint row_index) {
  ui::android::NotifyAndroidListSelectionFromJava(native_presenter, row_index);
}

JNINativeMethod const kUiMethods[] = {
    {"nativeOnPushButtonClick", "(J)V",
     reinterpret_cast<void*>(&NativeOnPushButtonClick)},
    {"nativeOnListSelection", "(JI)V",
     reinterpret_cast<void*>(&NativeOnListSelection)},
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
}  // namespace apptraverse::example::minimal

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::android::EnsureAndroidUiPresenterRegistration();

  using apptraverse::example::minimal::kNativeMethods;
  using apptraverse::example::minimal::kNativeRuntimeClass;
  using apptraverse::example::minimal::kUiMethods;
  using apptraverse::example::minimal::kAndroidUiClass;
  using apptraverse::example::minimal::RegisterNatives;

  if (!RegisterNatives(
          env, kNativeRuntimeClass, kNativeMethods,
          static_cast<jint>(sizeof(kNativeMethods) / sizeof(kNativeMethods[0])))) {
    apptraverse::android::LogError("JNI_OnLoad: NativeRuntime RegisterNatives failed");
    return JNI_ERR;
  }
  if (!RegisterNatives(
          env, kAndroidUiClass, kUiMethods,
          static_cast<jint>(sizeof(kUiMethods) / sizeof(kUiMethods[0])))) {
    apptraverse::android::LogError("JNI_OnLoad: AppTraverseAndroidUi RegisterNatives failed");
    return JNI_ERR;
  }
  apptraverse::android::LogInfo("JNI_OnLoad: minimal app natives registered");
  return JNI_VERSION_1_6;
}
