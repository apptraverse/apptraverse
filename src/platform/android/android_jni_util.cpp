#include "android_jni_util.h"
#include <cstdlib>

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse::ui::android {

[[noreturn]] void FailJni(JNIEnv* env, char const* operation) {
  if (env->ExceptionCheck()) env->ExceptionDescribe();
  WriteFatalStderr("fatal: Android JNI operation failed: ");
  WriteFatalStderr(operation);
  WriteFatalStderr("\n");
  std::abort();
}

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
  if (ref == nullptr) return;
  JNIEnv* env = RequireUiThreadEnv(host);
  env->DeleteGlobalRef(ref);
  ref = nullptr;
}

std::string JstringToUtf8(JNIEnv* env, jstring value) {
  if (value == nullptr) {
    return {};
  }
  auto cls = CheckedJni(env, "String class", [&] { return env->FindClass("java/lang/String"); });
  auto encode = CheckedJni(env, "String.getBytes", [&] {
    return env->GetMethodID(cls, "getBytes", "(Ljava/lang/String;)[B");
  });
  auto charset = CheckedJni(env, "UTF-8 charset", [&] { return env->NewStringUTF("UTF-8"); });
  auto bytes = static_cast<jbyteArray>(CheckedJni(env, "UTF-8 encode", [&] {
    return env->CallObjectMethod(value, encode, charset);
  }));
  auto size = env->GetArrayLength(bytes);
  std::string out(static_cast<std::size_t>(size), '\0');
  CheckedJni(env, "UTF-8 byte copy", [&] {
    env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(out.data()));
  });
  env->DeleteLocalRef(bytes);
  env->DeleteLocalRef(charset);
  env->DeleteLocalRef(cls);
  return out;
}

jstring Utf8ToJstring(JNIEnv* env, char const* utf8) {
  return Utf8ToJstring(env, std::string_view{utf8});
}

jstring Utf8ToJstring(JNIEnv* env, std::string_view utf8) {
  // JNI NewStringUTF/GetStringUTFChars use modified UTF-8, not the canonical
  // scalar UTF-8 shared by every model/backend. Java's UTF-8 codec also handles
  // supplementary characters and embedded NUL without ad-hoc model conversion.
  auto cls = CheckedJni(env, "String class", [&] { return env->FindClass("java/lang/String"); });
  auto decode = CheckedJni(env, "String UTF-8 constructor", [&] {
    return env->GetMethodID(cls, "<init>", "([BLjava/lang/String;)V");
  });
  auto charset = CheckedJni(env, "UTF-8 charset", [&] { return env->NewStringUTF("UTF-8"); });
  auto bytes = CheckedJni(env, "UTF-8 byte array", [&] {
    return env->NewByteArray(static_cast<jsize>(utf8.size()));
  });
  CheckedJni(env, "UTF-8 byte copy", [&] {
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(utf8.size()),
                           reinterpret_cast<jbyte const*>(utf8.data()));
  });
  auto text = static_cast<jstring>(CheckedJni(env, "UTF-8 decode", [&] {
    return env->NewObject(cls, decode, bytes, charset);
  }));
  env->DeleteLocalRef(bytes);
  env->DeleteLocalRef(charset);
  env->DeleteLocalRef(cls);
  return text;
}

ViewContentSize ReadViewContentSize(JNIEnv* env, jobject view) {
  ViewContentSize out;
  if (view == nullptr) {
    return out;
  }
  jclass ui_class =
      CheckedJni(env, "FindClass", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  jmethodID content_size = CheckedJni(env, "GetStaticMethodID", [&] { return env->GetStaticMethodID(
      ui_class, "contentSizeForView", "(Landroid/view/View;)[I"); });
  auto size_array = static_cast<jintArray>(CheckedJni(env, "CallStaticObjectMethod", [&] { return env->CallStaticObjectMethod(
      ui_class, content_size, view); }));
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(ui_class); });
  jsize const len = CheckedJni(env, "GetArrayLength", [&] { return env->GetArrayLength(size_array); });
  if (len >= 2) {
    jint const* elements = CheckedJni(env, "GetIntArrayElements", [&] { return env->GetIntArrayElements(size_array, nullptr); });
    out.width = elements[0];
    out.height = elements[1];
    CheckedJni(env, "ReleaseIntArrayElements", [&] { return env->ReleaseIntArrayElements(size_array, const_cast<jint*>(elements),
                                 JNI_ABORT); });
  } else FailJni(env, "contentSizeForView result");
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(size_array); });
  return out;
}

std::int32_t ReadActivityDensityDpi(JNIEnv* env, jobject activity) {
  jclass activity_class = CheckedJni(env, "GetObjectClass", [&] { return env->GetObjectClass(activity); });
  jmethodID get_resources = CheckedJni(env, "GetMethodID", [&] { return env->GetMethodID(
      activity_class, "getResources", "()Landroid/content/res/Resources;"); });
  jobject resources = CheckedJni(env, "CallObjectMethod", [&] { return env->CallObjectMethod(activity, get_resources); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(activity_class); });
  jclass res_class = CheckedJni(env, "GetObjectClass", [&] { return env->GetObjectClass(resources); });
  // API 34+ removed Resources#getDisplayMetrics(DisplayMetrics out).
  // Use the no-arg overload that returns the shared DisplayMetrics instance.
  jmethodID get_metrics = CheckedJni(env, "GetMethodID", [&] { return env->GetMethodID(
      res_class, "getDisplayMetrics", "()Landroid/util/DisplayMetrics;"); });
  jobject metrics = CheckedJni(env, "CallObjectMethod", [&] { return env->CallObjectMethod(resources, get_metrics); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(resources); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(res_class); });
  jclass dm_class = CheckedJni(env, "GetObjectClass", [&] { return env->GetObjectClass(metrics); });
  jfieldID dpi_field = CheckedJni(env, "GetFieldID", [&] { return env->GetFieldID(dm_class, "densityDpi", "I"); });
  jint const dpi = CheckedJni(env, "GetIntField", [&] { return env->GetIntField(metrics, dpi_field); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(dm_class); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(metrics); });
  return static_cast<std::int32_t>(dpi);
}

}  // namespace apptraverse::ui::android
