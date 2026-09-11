#include <jni.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "apptraverse/object_macros.h"

#include "android_log.h"
#include "android_surface_presenter.h"
#include "native_runtime.h"
#include "surfaces_ui_bridge.h"

namespace apptraverse::android {
namespace {

constexpr char const kNativeRuntimeClass[] =
    "com/apptraverse/surfaces/NativeRuntime";
constexpr char const kNativeCreateSignature[] =
    "(Ljava/lang/String;Lcom/apptraverse/surfaces/NativeUiBridge;)J";

NativeRuntime* FromHandle(jlong handle) {
  return reinterpret_cast<NativeRuntime*>(handle);
}

jlong NativeCreate(JNIEnv* env, jclass, jstring files_dir, jobject ui_bridge) {
  // Single startup point for the object-class registrations of this process.
  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureAndroidSurfacePresenterRegistration();

  auto const* chars = env->GetStringUTFChars(files_dir, nullptr);
  auto const files_path = std::string{chars};
  env->ReleaseStringUTFChars(files_dir, chars);

  auto bridge = MakeSurfacesUiBridge(env, ui_bridge);
  if (!bridge.is_valid()) {
    return 0;
  }

  auto state_dir = std::filesystem::path{files_path} / "surfaces_state";
  auto runtime =
      std::make_unique<NativeRuntime>(std::move(state_dir), std::move(bridge));
  LogMarker("SURFACES_NATIVE_RUNTIME_CREATED");
  return reinterpret_cast<jlong>(runtime.release());
}

void NativeRunModel(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->RunModel();
}

void NativeConsumePublication(JNIEnv*, jclass, jlong handle, jint kind) {
  FromHandle(handle)->ConsumePublication(
      kind == 0 ? SurfacesPublicationKind::Initial
                : SurfacesPublicationKind::Incremental);
}

void NativeAddFromSurface(JNIEnv*, jclass, jlong handle, jlong surface_id) {
  FromHandle(handle)->AddFromSurface(static_cast<std::uint32_t>(surface_id));
}

void NativeRemoveSurface(JNIEnv*, jclass, jlong handle, jlong surface_id) {
  FromHandle(handle)->RemoveSurface(static_cast<std::uint32_t>(surface_id));
}

void NativePageShown(JNIEnv*, jclass, jlong handle, jlong surface_id) {
  FromHandle(handle)->PageShown(static_cast<std::uint32_t>(surface_id));
}

void NativePersistState(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->PersistState();
}

void NativeReportPresentationSize(JNIEnv*, jclass, jlong handle, jint width,
                                  jint height) {
  FromHandle(handle)->ReportPresentationSize(static_cast<std::int32_t>(width),
                                             static_cast<std::int32_t>(height));
}

void NativeRequestStop(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->RequestStop();
}

void NativeUnloadUi(JNIEnv*, jclass, jlong handle) {
  FromHandle(handle)->UnloadUi();
}

void NativeDestroy(JNIEnv*, jclass, jlong handle) { delete FromHandle(handle); }

JNINativeMethod const kNativeMethods[] = {
    {"nativeCreate", kNativeCreateSignature,
     reinterpret_cast<void*>(&NativeCreate)},
    {"nativeRunModel", "(J)V", reinterpret_cast<void*>(&NativeRunModel)},
    {"nativeConsumePublication", "(JI)V",
     reinterpret_cast<void*>(&NativeConsumePublication)},
    {"nativeAddFromSurface", "(JJ)V",
     reinterpret_cast<void*>(&NativeAddFromSurface)},
    {"nativeRemoveSurface", "(JJ)V",
     reinterpret_cast<void*>(&NativeRemoveSurface)},
    {"nativePageShown", "(JJ)V", reinterpret_cast<void*>(&NativePageShown)},
    {"nativePersistState", "(J)V",
     reinterpret_cast<void*>(&NativePersistState)},
    {"nativeReportPresentationSize", "(JII)V",
     reinterpret_cast<void*>(&NativeReportPresentationSize)},
    {"nativeRequestStop", "(J)V", reinterpret_cast<void*>(&NativeRequestStop)},
    {"nativeUnloadUi", "(J)V", reinterpret_cast<void*>(&NativeUnloadUi)},
    {"nativeDestroy", "(J)V", reinterpret_cast<void*>(&NativeDestroy)},
};

}  // namespace
}  // namespace apptraverse::android

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
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

  auto const method_count =
      static_cast<jint>(sizeof(apptraverse::android::kNativeMethods) /
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
