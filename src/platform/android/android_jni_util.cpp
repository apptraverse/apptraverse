#include "android_jni_util.h"

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse::ui::android {

JNIEnv* RequireUiThreadEnv(AndroidPresentationHost const& host) {
  if (host.vm == nullptr) {
    WriteFatalStderr("fatal: AndroidPresentationHost missing JavaVM\n");
    std::abort();
  }
  JNIEnv* env = nullptr;
  if (host.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    WriteFatalStderr("fatal: Android UI presenter off Android UI thread\n");
    std::abort();
  }
  return env;
}

void ClearGlobalRef(AndroidPresentationHost const& host, jobject& ref) {
  if (ref == nullptr || host.vm == nullptr) {
    ref = nullptr;
    return;
  }
  JNIEnv* env = nullptr;
  if (host.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
    env->DeleteGlobalRef(ref);
  }
  ref = nullptr;
}

std::string JstringToUtf8(JNIEnv* env, jstring value) {
  if (value == nullptr) {
    return {};
  }
  char const* chars = env->GetStringUTFChars(value, nullptr);
  std::string out{chars};
  env->ReleaseStringUTFChars(value, chars);
  return out;
}

jstring Utf8ToJstring(JNIEnv* env, char const* utf8) {
  return env->NewStringUTF(utf8 != nullptr ? utf8 : "");
}

ViewContentSize ReadViewContentSize(JNIEnv* env, jobject view) {
  ViewContentSize out;
  if (view == nullptr) {
    return out;
  }
  jclass ui_class =
      env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID content_size = env->GetStaticMethodID(
      ui_class, "contentSizeForView", "(Landroid/view/View;)[I");
  auto size_array = static_cast<jintArray>(env->CallStaticObjectMethod(
      ui_class, content_size, view));
  env->DeleteLocalRef(ui_class);
  if (size_array == nullptr) {
    return out;
  }
  jsize const len = env->GetArrayLength(size_array);
  if (len >= 2) {
    jint const* elements = env->GetIntArrayElements(size_array, nullptr);
    out.width = elements[0];
    out.height = elements[1];
    env->ReleaseIntArrayElements(size_array, const_cast<jint*>(elements),
                                 JNI_ABORT);
  }
  env->DeleteLocalRef(size_array);
  return out;
}

std::int32_t ReadActivityDensityDpi(JNIEnv* env, jobject activity) {
  if (activity == nullptr) {
    return 0;
  }
  jclass activity_class = env->GetObjectClass(activity);
  jmethodID get_resources = env->GetMethodID(
      activity_class, "getResources", "()Landroid/content/res/Resources;");
  jobject resources = env->CallObjectMethod(activity, get_resources);
  env->DeleteLocalRef(activity_class);
  if (resources == nullptr) {
    return 0;
  }
  jclass res_class = env->GetObjectClass(resources);
  // API 34+ removed Resources#getDisplayMetrics(DisplayMetrics out).
  // Use the no-arg overload that returns the shared DisplayMetrics instance.
  jmethodID get_metrics = env->GetMethodID(
      res_class, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;");
  jobject metrics = env->CallObjectMethod(resources, get_metrics);
  env->DeleteLocalRef(resources);
  env->DeleteLocalRef(res_class);
  if (metrics == nullptr) {
    return 0;
  }
  jclass dm_class = env->GetObjectClass(metrics);
  jfieldID dpi_field = env->GetFieldID(dm_class, "densityDpi", "I");
  jint const dpi = env->GetIntField(metrics, dpi_field);
  env->DeleteLocalRef(dm_class);
  env->DeleteLocalRef(metrics);
  return static_cast<std::int32_t>(dpi);
}

}  // namespace apptraverse::ui::android
