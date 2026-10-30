#include "minimal_native_runtime.h"

#include "android_log.h"

#include "minimal_app_android_lifecycle.h"

namespace apptraverse::example::minimal {

MinimalNativeRuntime::MinimalNativeRuntime(std::filesystem::path state_dir,
                                             android::AndroidUiBridge ui_bridge)
    : state_dir_{std::move(state_dir)}, ui_bridge_{std::move(ui_bridge)} {}

MinimalNativeRuntime::~MinimalNativeRuntime() = default;

void MinimalNativeRuntime::SetPresentationRoot(JNIEnv* env, jobject activity,
                                                 jobject content_root) {
  if (!host_) {
    android::AndroidApplicationHostCallbacks callbacks;
    callbacks.run_model = [this](ApplicationPublishedCallback on_published) {
      return RunMinimalApplicationModel(state_dir_, runtime_,
                                        std::move(on_published), true);
    };
    callbacks.after_initial = [](ae::Obj&) {
      apptraverse::android::LogInfo("MINIMAL_APP_INITIAL_RENDER_OK");
    };
    host_ = std::make_unique<android::AndroidApplicationHost>(
        runtime_, presentation_host_, std::move(ui_bridge_), std::move(callbacks));
  }
  host_->SetPresentationRoot(env, activity, content_root);
}

void MinimalNativeRuntime::StartModelThread() { host_->StartModelThread(); }

void MinimalNativeRuntime::ConsumePublication(int kind) {
  if (!host_) {
    return;
  }
  host_->ConsumePublication(kind == 0 ? ApplicationPublicationKind::Initial
                                       : ApplicationPublicationKind::Incremental);
}

}  // namespace apptraverse::example::minimal
