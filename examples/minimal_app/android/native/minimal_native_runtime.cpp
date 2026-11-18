#include "minimal_native_runtime.h"

#include "android_jni_util.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"

#if defined(APPTRAVERSE_MINIMAL_TEST_HOOKS)
#include "minimal_app_android_checkpoint_test_gate.h"
#endif
#include "minimal_app_android_lifecycle.h"
#include "minimal_app_persistence.h"

namespace apptraverse::example::minimal {

MinimalNativeRuntime::MinimalNativeRuntime(std::filesystem::path state_dir,
                                             android::AndroidUiBridge ui_bridge)
    : state_dir_{std::move(state_dir)}, ui_bridge_{std::move(ui_bridge)} {
#if defined(APPTRAVERSE_DEBUG_FRESH_START)
  state_dir_ /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
#endif
}

MinimalNativeRuntime::~MinimalNativeRuntime() {
  if (host_) {
    host_->JoinModelThreadForRelease();
  }
}

void MinimalNativeRuntime::SetPresentationRoot(JNIEnv* env, jobject activity,
                                                 jobject content_root) {
  if (!host_) {
    startup_environment_ = ui::android::CaptureAndroidDeviceEnvironment(env, activity);
    android::AndroidApplicationHostCallbacks callbacks;
    callbacks.run_model = [this](ApplicationPublishedCallback on_published) {
      return RunMinimalApplicationModel(user_storage_, local_storage_, state_dir_, runtime_,
                                        std::move(on_published), &startup_environment_);
    };
    callbacks.after_initial = [this](ae::Obj& root) {
      ApplyPagePresentation(root);
    };
    callbacks.after_incremental = [this](ae::Obj& root) { ApplyPagePresentation(root); };
    callbacks.persist_model = [this](ae::Domain& domain) -> int {
#if defined(APPTRAVERSE_MINIMAL_TEST_HOOKS)
      WaitAndroidCheckpointPersistTestGateIfEnabled();
#endif
      return PersistAndroidMinimalUser(domain, user_storage_, local_storage_, state_dir_);
    };
    host_ = std::make_unique<android::AndroidApplicationHost>(
        runtime_, presentation_host_, std::move(ui_bridge_), std::move(callbacks));

  }
  host_->SetPresentationRoot(env, activity, content_root);
  if (auto* session = host_->ui_session()) {
    ApplyPagePresentation(session->root());
  }
}

void MinimalNativeRuntime::ApplyPagePresentation(ae::Obj& root) {
  auto& application = static_cast<MinimalApplication&>(root);
  auto* env = ui::android::RequireUiThreadEnv(presentation_host_);
  auto cls = ui::android::CheckedJni(env, "View class", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  auto visibility = ui::android::CheckedJni(env, "View.setVisibility", [&] { return env->GetStaticMethodID(cls,"setClientMountVisible","(Landroid/widget/FrameLayout;Z)V"); });
  auto show = [&](ui::UiDesktopWindow& window) {
    auto& presenter = static_cast<ui::android::AndroidDesktopWindowPresenter&>(*window.presenter);
    auto mount = ui::android::AndroidPresentationHost::ClientMount(presenter);
    bool const visible = window.obj_id == application.ActivePageId();
    ui::android::CheckedJni(env, "page visibility", [&] { env->CallStaticVoidMethod(cls,visibility,mount,visible ? JNI_TRUE : JNI_FALSE); });
  };
  show(*application.main_window);
  if (application.auxiliary_presented_open) show(*application.auxiliary.window);
  env->DeleteLocalRef(cls);
}

void MinimalNativeRuntime::ClearPresentationRoot(JNIEnv* env, jobject activity) {
  if (host_) host_->ClearPresentationRoot(env, activity);
}

void MinimalNativeRuntime::StartModelThread() { host_->StartModelThread(); }

void MinimalNativeRuntime::ConsumePublication(int kind) {
  if (!host_) {
    return;
  }
  host_->ConsumePublication(kind == 0 ? ApplicationPublicationKind::Initial
                                       : ApplicationPublicationKind::Incremental);
}

void MinimalNativeRuntime::RequestBackgroundSave() {
  if (!host_) {
    return;
  }
  host_->FlushPendingBackgroundSaves();
}

}  // namespace apptraverse::example::minimal
