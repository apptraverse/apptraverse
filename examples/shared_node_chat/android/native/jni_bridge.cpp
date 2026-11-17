#include <jni.h>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include "android_jni_util.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_bridge.h"
#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/platform/android/android_ui_text_control_presenter.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"
#include "chat_ui_session.h"
namespace gui = apptraverse::chat::gui;
using namespace apptraverse::ui::android;
namespace {
JavaVM* vm{};
jclass activity_class{};
jmethodID enqueue{}, clipboard{};
AndroidPresentationHost host;
std::uint64_t copied{};
std::unique_ptr<gui::Session> session;
void StopOwnedSession(JNIEnv* env) {
  // Presenters still refer to the old owner while their native views unload.
  session.reset();
  copied = 0;
  if (host.content_root) env->DeleteGlobalRef(host.content_root);
  if (host.activity) env->DeleteGlobalRef(host.activity);
  host.content_root = nullptr;
  host.activity = nullptr;
}

void Changed() {
  JNIEnv* env{};
  bool attached = false;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) std::abort();
    attached = true;
  }
  CheckedJni(env, "queue chat publication",
             [&] { env->CallStaticVoidMethod(activity_class, enqueue); });
  if (attached) vm->DetachCurrentThread();
}
}  // namespace
extern "C" JNIEXPORT void JNICALL
Java_com_apptraverse_sharedchat_MainActivity_startNative(
    JNIEnv* env, jobject activity, jstring path, jstring name, jboolean is_host,
    jstring invite, jboolean load_only, jstring push_uid) {
  if (!activity_class) {
    env->GetJavaVM(&vm);
    auto cls = env->GetObjectClass(activity);
    activity_class = static_cast<jclass>(
        CheckedJni(env, "chat class", [&] { return env->NewGlobalRef(cls); }));
    env->DeleteLocalRef(cls);
    enqueue = CheckedJni(env, "chat notification method", [&] {
      return env->GetStaticMethodID(activity_class, "enqueuePublication",
                                    "()V");
    });
    clipboard = CheckedJni(env, "clipboard method", [&] {
      return env->GetMethodID(activity_class, "copyUid",
                              "(Ljava/lang/String;)V");
    });
    if (!apptraverse::android::RegisterAndroidUiNativeMethods(env))
      std::abort();
  }
  if (session) {
    auto cls = env->GetObjectClass(host.activity);
    auto method = env->GetMethodID(cls, "isFinishing", "()Z");
    bool const finishing = CheckedJni(
        env, "previous chat owner finishing",
        [&] { return env->CallBooleanMethod(host.activity, method); });
    env->DeleteLocalRef(cls);
    if (finishing) StopOwnedSession(env);
  }
  if (session) {
    auto old_activity = host.activity, old_root = host.content_root;
    host.activity = env->NewGlobalRef(activity);
    auto cls = env->GetObjectClass(activity);
    auto method =
        env->GetMethodID(cls, "contentRoot", "()Landroid/view/ViewGroup;");
    auto root = env->CallObjectMethod(activity, method);
    host.content_root = env->NewGlobalRef(root);
    env->DeleteLocalRef(root);
    env->DeleteLocalRef(cls);
    session->RemountPresentation();
    env->DeleteGlobalRef(old_activity);
    env->DeleteGlobalRef(old_root);
    return;
  }
  if (!session) {
    copied = 0;
    host.vm = vm;
    host.activity = env->NewGlobalRef(activity);
    auto cls = env->GetObjectClass(activity);
    auto surface =
        env->GetMethodID(cls, "contentRoot", "()Landroid/view/ViewGroup;");
    auto root = env->CallObjectMethod(activity, surface);
    host.content_root = env->NewGlobalRef(root);
    env->DeleteLocalRef(root);
    env->DeleteLocalRef(cls);
    EnsureAndroidUiPresenterRegistration();
    auto environment = CaptureAndroidDeviceEnvironment(env, activity);
    gui::ConfigureUiFactories(
        {[](ae::Domain& d) {
           return AndroidDesktopWindowPresenter::ptr::Create(ae::CreateWith{d});
         },
         [](ae::Domain& d) {
           return AndroidPushButtonPresenter::ptr::Create(ae::CreateWith{d});
         },
         [](ae::Domain& d) {
           return AndroidTextControlPresenter::ptr::Create(ae::CreateWith{d});
         },
         [](ae::Domain& d) {
           return AndroidListContainerPresenter::ptr::Create(ae::CreateWith{d});
         },
         [environment](apptraverse::ui::UiDeviceState& state) {
           state.ObserveEnvironment(
               environment, apptraverse::ui::UiEnvironmentReason::kStartup);
         }});
    session = std::make_unique<gui::Session>(
        JstringToUtf8(env, path), JstringToUtf8(env, name), is_host,
        JstringToUtf8(env, invite), Changed, load_only);
    session->ConfigurePush(JstringToUtf8(env, push_uid));
    session->Start();
  }
}
extern "C" JNIEXPORT void JNICALL
Java_com_apptraverse_sharedchat_MainActivity_renderNative(JNIEnv* env,
                                                          jobject activity,
                                                          jboolean remount) {
  if (!session) return;
  session->ApplyPublication(host.presenter_host());
  if (auto* model = session->Mirror(); model && model->copy_request != copied) {
    copied = model->copy_request;
    auto text = Utf8ToJstring(env, model->connection.uid);
    CheckedJni(env, "copy UID",
               [&] { env->CallVoidMethod(activity, clipboard, text); });
    env->DeleteLocalRef(text);
    session->Proxy().Invoke(gui::RootId(), &gui::ChatUi::ClipboardResult, true);
  }
}
extern "C" JNIEXPORT jboolean JNICALL
Java_com_apptraverse_sharedchat_MainActivity_checkpointNative(
    JNIEnv* env, jobject activity) {
  if (!session || !env->IsSameObject(activity, host.activity)) return true;
  std::mutex mutex;
  std::condition_variable ready;
  bool done = false, ok = false;
  session->Checkpoint([&](bool result) {
    std::lock_guard lock(mutex);
    ok = result;
    done = true;
    ready.notify_one();
  });
  std::unique_lock lock(mutex);
  ready.wait(lock, [&] { return done; });
  return ok;
}
extern "C" JNIEXPORT void JNICALL
Java_com_apptraverse_sharedchat_MainActivity_stopNative(JNIEnv* env,
                                                        jobject activity) {
  if (!host.activity || !env->IsSameObject(activity, host.activity)) return;
  StopOwnedSession(env);
}

extern "C" JNIEXPORT void JNICALL
Java_com_apptraverse_sharedchat_MainActivity_registerPushTokenNative(
    JNIEnv* env, jclass, jstring token) {
  if (session) session->RegisterPushToken(JstringToUtf8(env, token));
}
