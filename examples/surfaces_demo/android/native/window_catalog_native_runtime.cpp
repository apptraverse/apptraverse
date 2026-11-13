#include "window_catalog_native_runtime.h"

#include "window_catalog_android_lifecycle.h"
#include "window_catalog_android_persistence.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"

#include "android_jni_util.h"
#include "android_log.h"

#include "apptraverse/platform/android/android_ui_window_presenter.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse::example::surfaces {
namespace {
using ui::android::CheckedJni;

ui::UiPresentedWindow& Presented(CatalogWindow& window) {
  return static_cast<ui::UiPresentedWindow&>(window);
}

ui::android::AndroidDesktopWindowPresenter* AndroidPresenterForWindow(
    CatalogWindow& window) {
  return &static_cast<ui::android::AndroidDesktopWindowPresenter&>(
      *Presented(window).presenter.operator->());
}

void SetMountVisible(ui::android::AndroidPresentationHost& host,
                     jobject mount, bool visible) {
  JNIEnv* env = ui::android::RequireUiThreadEnv(host);
  jclass view_class = CheckedJni(env, "FindClass", [&] { return env->FindClass("android/view/View"); });
  jmethodID set_visibility =
      CheckedJni(env, "GetMethodID", [&] { return env->GetMethodID(view_class, "setVisibility", "(I)V"); });
  jint const visibility = visible ? 0 : 8;
  CheckedJni(env, "CallVoidMethod", [&] { return env->CallVoidMethod(mount, set_visibility, visibility); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(view_class); });
}

void SetActivityTitle(ui::android::AndroidPresentationHost& host,
                      char const* title_utf8) {
  JNIEnv* env = ui::android::RequireUiThreadEnv(host);
  jclass ui_class =
      CheckedJni(env, "FindClass", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  jmethodID set_title = CheckedJni(env, "GetStaticMethodID", [&] { return env->GetStaticMethodID(
      ui_class, "setActivityTitle", "(Landroid/app/Activity;Ljava/lang/String;)V"); });
  jstring jtitle = ui::android::Utf8ToJstring(env, title_utf8);
  CheckedJni(env, "CallStaticVoidMethod", [&] { return env->CallStaticVoidMethod(ui_class, set_title, host.activity, jtitle); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(jtitle); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(ui_class); });
}

std::string ResolveWindowTitle(CatalogWindow& window) {
  auto& presented = Presented(window);
  int const language = presented.presenter->locale->language_index();
  return presented.presenter->ResolvePresentedWindowTitle(language);
}

apptraverse::WindowCatalog& CatalogRoot(ae::Obj& root) {
  return static_cast<apptraverse::WindowCatalog&>(root);
}

}  // namespace

WindowCatalogNativeRuntime::WindowCatalogNativeRuntime(
    std::filesystem::path state_dir, android::WindowCatalogUiBridge ui_bridge,
    android::AndroidUiBridge android_ui_bridge)
    : state_dir_{std::move(state_dir)},
      ui_bridge_{std::move(ui_bridge)},
      android_ui_bridge_{std::move(android_ui_bridge)},
      profile_{state_dir_} {
  presentation_host_.on_window_client_size =
      [this](ui::android::AndroidDesktopWindowPresenter& presenter,
             std::int32_t width, std::int32_t height, std::int32_t dpi) {
        ae::ObjId const catalog_id{
            window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
        ae::ObjId const main_id{
            window_catalog_demo::ToObjId(window_catalog_demo::ObjId::MainWindow)};
        if (presenter.window.id() == main_id) {
          host_->model_proxy().Invoke(catalog_id,
              &WindowCatalog::SubmitMainClientViewportFromUi, width, height, dpi);
        } else {
          host_->model_proxy().Invoke(catalog_id,
              &WindowCatalog::SubmitHolderChildWindowClientSizeFromUi,
              presenter.window.id().id(), width, height, dpi);
        }
      };
}

WindowCatalogNativeRuntime::~WindowCatalogNativeRuntime() {
  if (host_) {
    host_->JoinModelThreadForRelease();
  }
}

void WindowCatalogNativeRuntime::SetPresentationRoot(JNIEnv* env,
                                                      jobject activity,
                                                      jobject content_root) {
  if (!host_) {
    android::AndroidApplicationHostCallbacks callbacks;
    callbacks.run_model = [this](ApplicationPublishedCallback on_published) {
      return RunWindowCatalogAndroidModel(profile_, runtime_,
                                          std::move(on_published));
    };
    callbacks.after_initial = [this](ae::Obj& root) {
      SyncMobilePagerChrome(CatalogRoot(root));
      PublishMobilePages();
      apptraverse::android::LogInfo("WINDOW_CATALOG_INITIAL_RENDER_OK");
    };
    callbacks.after_incremental = [this](ae::Obj& root) {
      SyncMobilePagerChrome(CatalogRoot(root));
      PublishMobilePages();
    };
    callbacks.persist_model = [this](ae::Domain& domain) -> int {
      return PersistWindowCatalogEmbeddedUserOverlayFromDomain(domain,
                                                               profile_);
    };
    host_ = std::make_unique<android::AndroidApplicationHost>(
        runtime_, presentation_host_, std::move(android_ui_bridge_),
        std::move(callbacks));
  }
  host_->SetPresentationRoot(env, activity, content_root);
  if (host_->ui_session()) {
    SyncMobilePagerChrome(CatalogRoot(host_->ui_session()->root()));
    PublishMobilePages();
  }
}

void WindowCatalogNativeRuntime::StartModelThread() {
  host_->StartModelThread();
}

void WindowCatalogNativeRuntime::ConsumePublication(int kind) {
  host_->ConsumePublication(kind == 0 ? ApplicationPublicationKind::Initial
                                       : ApplicationPublicationKind::Incremental);
}

void WindowCatalogNativeRuntime::FlushPendingBackgroundSaves() {
  if (host_) {
    host_->FlushPendingBackgroundSaves();
  }
}

void WindowCatalogNativeRuntime::MobileSwipe(int delta) {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  host_->model_proxy().Invoke(
      catalog_id, &apptraverse::WindowCatalog::SubmitWindowNavigationFromUi, delta);
}

void WindowCatalogNativeRuntime::SyncMobilePagerChrome(
    apptraverse::WindowCatalog& catalog) {
  CatalogWindow::ptr active = catalog.ResolveActiveWindow();
  ae::ObjId const active_id = active.id();

  auto sync_one = [&](CatalogWindow::ptr const& window_ref, bool show) {
    if (!window_ref.is_loaded()) {
      return;
    }
    auto* presenter = AndroidPresenterForWindow(*window_ref);
    SetMountVisible(presentation_host_,
                    presentation_host_.ClientMount(*presenter), show);
  };

  sync_one(catalog.persisted.main_window,
           active_id == catalog.persisted.main_window.id());
  catalog.persisted.list.Load();
  for (auto const& row : catalog.persisted.list->rows) {
    row.Load();
    auto* holder = static_cast<WindowHolder*>(&*row);
    bool const show =
        holder->window.is_loaded() && holder->window.id() == active_id;
    sync_one(holder->window, show);
  }

  CatalogWindow& title_window =
      active_id == catalog.persisted.main_window.id()
          ? *catalog.persisted.main_window.Load()
          : *active.Load();
  SetActivityTitle(presentation_host_, ResolveWindowTitle(title_window).c_str());
}

void WindowCatalogNativeRuntime::PublishMobilePages() {
  apptraverse::WindowCatalog& catalog = CatalogRoot(host_->ui_session()->root());
  std::vector<CatalogWindow::ptr> const ring = catalog.BuildWindowNavigationRing();
  std::vector<std::int64_t> ids;
  ids.reserve(ring.size());
  for (auto const& window : ring) {
    ids.push_back(static_cast<std::int64_t>(window.id().id()));
  }
  CatalogWindow::ptr active = catalog.ResolveActiveWindow();
  std::int64_t const active_id = static_cast<std::int64_t>(active.id().id());
  ui_bridge_.PostMobilePages(ids, active_id);
}

}  // namespace apptraverse::example::surfaces
