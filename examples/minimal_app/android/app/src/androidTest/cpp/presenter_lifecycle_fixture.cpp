// Test-only JNI fixture for Android presenter teardown: a real
// ApplicationUiSession over the MinimalApp graph, driven on the UI thread by
// PresenterLifecycleInstrumentedTest.

#include <jni.h>

#include <memory>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_android_graph.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {
namespace {

class PresenterLifecycleFixture {
 public:
  PresenterLifecycleFixture(JNIEnv* env, jobject content_root) {
    env->GetJavaVM(&host_.vm);
    host_.content_root = env->NewGlobalRef(content_root);
    model_root_ = BuildAndroidMinimalApplicationGraph(model_domain_);
    PrepareMinimalApplicationForRun(*model_root_);
    ByteSink initial;
    SerializeInitialPublication(*model_root_, initial);
    session_ = std::make_unique<ApplicationUiSession>(
        initial.bytes, host_.presenter_host(), proxy_);
  }

  ~PresenterLifecycleFixture() {
    session_.reset();
    JNIEnv* env = nullptr;
    host_.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    env->DeleteGlobalRef(host_.content_root);
  }

  void Remount() { session_->RemountPresentation(); }

  // Model-side row removal published the same way ApplicationModelRuntime
  // does: structural publication of the application root, applied by the
  // UI session.
  void RemoveMainListRow(int row_index) {
    auto& list = *model_root_->row_list;
    auto remove =
        ui::RemoveUiListRowEvent::ptr::Create(ae::CreateWith{model_domain_});
    remove->row_id = list.rows[static_cast<std::size_t>(row_index)].id();
    list.Commit(std::move(remove));
    ByteSink structural;
    SerializeStructuralNodePublication(*model_root_, structural);
    session_->ApplyStructural(structural.bytes);
  }

  int enqueue_count() const { return enqueue_count_; }

 private:
  ae::RamDomainStorage model_storage_;
  ae::Domain model_domain_{model_storage_};
  MinimalApplication::ptr model_root_;
  int enqueue_count_{0};
  ModelObjectProxy proxy_{
      [this](ModelObjectProxy::ModelWork) { ++enqueue_count_; }};
  ui::android::AndroidPresentationHost host_;
  std::unique_ptr<ApplicationUiSession> session_;
};

PresenterLifecycleFixture* FromHandle(jlong handle) {
  return reinterpret_cast<PresenterLifecycleFixture*>(handle);
}

jlong NativeCreate(JNIEnv* env, jclass, jobject content_root) {
  return reinterpret_cast<jlong>(new PresenterLifecycleFixture(env, content_root));
}

void NativeRemount(JNIEnv*, jclass, jlong handle) { FromHandle(handle)->Remount(); }

void NativeRemoveMainListRow(JNIEnv*, jclass, jlong handle, jint row_index) {
  FromHandle(handle)->RemoveMainListRow(static_cast<int>(row_index));
}

jint NativeEnqueueCount(JNIEnv*, jclass, jlong handle) {
  return static_cast<jint>(FromHandle(handle)->enqueue_count());
}

void NativeDestroy(JNIEnv*, jclass, jlong handle) { delete FromHandle(handle); }

void NativeOnPushButtonClick(JNIEnv*, jclass, jlong native_presenter) {
  ui::android::NotifyAndroidPushButtonClickedFromJava(native_presenter);
}

void NativeOnListSelection(JNIEnv*, jclass, jlong native_presenter,
                           jint row_index) {
  ui::android::NotifyAndroidListSelectionFromJava(native_presenter, row_index);
}

JNINativeMethod const kFixtureMethods[] = {
    {"nativeCreate", "(Landroid/view/ViewGroup;)J",
     reinterpret_cast<void*>(&NativeCreate)},
    {"nativeRemount", "(J)V", reinterpret_cast<void*>(&NativeRemount)},
    {"nativeRemoveMainListRow", "(JI)V",
     reinterpret_cast<void*>(&NativeRemoveMainListRow)},
    {"nativeEnqueueCount", "(J)I", reinterpret_cast<void*>(&NativeEnqueueCount)},
    {"nativeDestroy", "(J)V", reinterpret_cast<void*>(&NativeDestroy)},
};

// The app library registered these for its own presenter copy; Views created
// by this library's presenters must call back into this library.
JNINativeMethod const kUiMethods[] = {
    {"nativeOnPushButtonClick", "(J)V",
     reinterpret_cast<void*>(&NativeOnPushButtonClick)},
    {"nativeOnListSelection", "(JI)V",
     reinterpret_cast<void*>(&NativeOnListSelection)},
};

bool Register(JNIEnv* env, char const* class_name,
              JNINativeMethod const* methods, jint count) {
  jclass cls = env->FindClass(class_name);
  if (cls == nullptr) {
    return false;
  }
  jint const rc = env->RegisterNatives(cls, methods, count);
  env->DeleteLocalRef(cls);
  return rc == JNI_OK;
}

}  // namespace
}  // namespace apptraverse::example::minimal

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  using namespace apptraverse::example::minimal;
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::android::EnsureAndroidUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();
  if (!Register(env, "com/apptraverse/minimal/PresenterLifecycleFixture",
                kFixtureMethods,
                static_cast<jint>(sizeof(kFixtureMethods) /
                                  sizeof(kFixtureMethods[0]))) ||
      !Register(env, "com/apptraverse/ui/android/AppTraverseAndroidUi",
                kUiMethods,
                static_cast<jint>(sizeof(kUiMethods) / sizeof(kUiMethods[0])))) {
    return JNI_ERR;
  }
  return JNI_VERSION_1_6;
}
