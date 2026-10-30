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

}  // namespace apptraverse::ui::android
