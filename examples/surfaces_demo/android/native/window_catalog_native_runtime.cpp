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

ui::UiPresentedWindow& Presented(CatalogWindow& window) {
  return static_cast<ui::UiPresentedWindow&>(window);
}

ui::android::AndroidDesktopWindowPresenter* AndroidPresenterForWindow(
    CatalogWindow& window) {
  if (!Presented(window).presenter.is_loaded()) {
    return nullptr;
  }
  return &static_cast<ui::android::AndroidDesktopWindowPresenter&>(
      *Presented(window).presenter.operator->());
}

void SetMountVisible(ui::android::AndroidPresentationHost& host,
                     jobject mount, bool visible) {
  if (mount == nullptr) {
    return;
  }
  JNIEnv* env = ui::android::RequireUiThreadEnv(host);
  jclass view_class = env->FindClass("android/view/View");
  jmethodID set_visibility =
      env->GetMethodID(view_class, "setVisibility", "(I)V");
  jint const visibility = visible ? 0 : 8;
  env->CallVoidMethod(mount, set_visibility, visibility);
  env->DeleteLocalRef(view_class);
}

void SetActivityTitle(ui::android::AndroidPresentationHost& host,
                      char const* title_utf8) {
  if (host.activity == nullptr) {
    return;
  }
  JNIEnv* env = ui::android::RequireUiThreadEnv(host);
  jclass ui_class =
      env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi");
  jmethodID set_title = env->GetStaticMethodID(
      ui_class, "setActivityTitle", "(Landroid/app/Activity;Ljava/lang/String;)V");
  jstring jtitle = ui::android::Utf8ToJstring(env, title_utf8);
  env->CallStaticVoidMethod(ui_class, set_title, host.activity, jtitle);
  env->DeleteLocalRef(jtitle);
  env->DeleteLocalRef(ui_class);
}

std::string ResolveWindowTitle(CatalogWindow& window) {
  auto& presented = Presented(window);
  if (!presented.presenter.is_loaded() || !presented.presenter->title.is_valid()) {
    return {};
  }
  int const language = presented.presenter->locale->language_index();
  return presented.presenter->title->Resolve(language);
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
      profile_{state_dir_} {}

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
      ReportMainViewport();
      PublishMobilePages();
      apptraverse::android::LogInfo("WINDOW_CATALOG_INITIAL_RENDER_OK");
    };
    callbacks.after_incremental = [this](ae::Obj& root) {
      SyncMobilePagerChrome(CatalogRoot(root));
      ReportMainViewport();
      PublishMobilePages();
    };
    callbacks.persist_model = [this](ae::Domain& domain) -> int {
      return PersistWindowCatalogEmbeddedUserOverlayFromDomain(domain,
                                                               profile_.user);
    };
    host_ = std::make_unique<android::AndroidApplicationHost>(
        runtime_, presentation_host_, std::move(android_ui_bridge_),
        std::move(callbacks));
  }
  host_->SetPresentationRoot(env, activity, content_root);
}

void WindowCatalogNativeRuntime::StartModelThread() {
  host_->StartModelThread();
}

void WindowCatalogNativeRuntime::ConsumePublication(int kind) {
  host_->ConsumePublication(kind == 0 ? ApplicationPublicationKind::Initial
                                       : ApplicationPublicationKind::Incremental);
}

void WindowCatalogNativeRuntime::RequestBackgroundSave() {
  host_->RequestBackgroundSave();
}

void WindowCatalogNativeRuntime::MobilePageShown(std::uint32_t window_obj_id) {
  if (reported_active_for_ui_.is_valid() &&
      reported_active_for_ui_.id() == window_obj_id) {
    return;
  }
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  host_->model_proxy().Invoke(
      catalog_id, &apptraverse::WindowCatalog::SubmitMobileActiveFromUi,
      ae::ObjId{window_obj_id});
  reported_active_for_ui_ = ae::ObjId{window_obj_id};
  RequestBackgroundSave();
}

void WindowCatalogNativeRuntime::MobileSwipe(int delta) {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  host_->model_proxy().Invoke(
      catalog_id, &apptraverse::WindowCatalog::SubmitMobileSwipeFromUi, delta);
}

void WindowCatalogNativeRuntime::ReportMainViewport() {
  if (!host_ || !host_->ui_session()) {
    return;
  }
  apptraverse::WindowCatalog& catalog = CatalogRoot(host_->ui_session()->root());
  if (!catalog.main_window.is_valid() || !catalog.main_window.is_loaded()) {
    return;
  }
  auto* presenter = AndroidPresenterForWindow(*catalog.main_window);
  if (presenter == nullptr) {
    return;
  }
  jobject const mount = presentation_host_.ClientMount(*presenter);
  if (mount == nullptr) {
    return;
  }
  JNIEnv* env = ui::android::RequireUiThreadEnv(presentation_host_);
  ui::android::ViewContentSize const size =
      ui::android::ReadViewContentSize(env, mount);
  if (size.width <= 0 || size.height <= 0) {
    return;
  }
  if (size.width == catalog.main_client_viewport_w &&
      size.height == catalog.main_client_viewport_h) {
    return;
  }
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  host_->model_proxy().Invoke(
      catalog_id, &apptraverse::WindowCatalog::SubmitMainClientViewportFromUi,
      size.width, size.height);
}

void WindowCatalogNativeRuntime::SyncMobilePagerChrome(
    apptraverse::WindowCatalog& catalog) {
  if (!catalog.mobile_pager || !host_->ui_session()) {
    return;
  }
  CatalogWindow::ptr active = catalog.ResolveMobileActiveWindow();
  ae::ObjId const active_id = active.id();

  auto sync_one = [&](CatalogWindow::ptr const& window_ref, bool show) {
    if (!window_ref.is_valid() || !window_ref.is_loaded()) {
      return;
    }
    auto* presenter = AndroidPresenterForWindow(*window_ref);
    if (presenter == nullptr) {
      return;
    }
    SetMountVisible(presentation_host_,
                    presentation_host_.ClientMount(*presenter), show);
  };

  sync_one(catalog.main_window, active_id == catalog.main_window.id());
  catalog.list.Load();
  for (auto const& row : catalog.list->rows) {
    row.Load();
    auto* holder = static_cast<WindowHolder*>(&*row);
    if (!holder->window.is_valid()) {
      continue;
    }
    bool const show =
        holder->window.is_loaded() && holder->window.id() == active_id;
    sync_one(holder->window, show);
  }

  CatalogWindow& title_window = active_id == catalog.main_window.id()
                                    ? *catalog.main_window.Load()
                                    : *active.Load();
  SetActivityTitle(presentation_host_, ResolveWindowTitle(title_window).c_str());
}

void WindowCatalogNativeRuntime::PublishMobilePages() {
  if (!host_->ui_session()) {
    return;
  }
  apptraverse::WindowCatalog& catalog = CatalogRoot(host_->ui_session()->root());
  if (!catalog.mobile_pager) {
    return;
  }
  std::vector<CatalogWindow::ptr> const ring = catalog.BuildMobileRing();
  std::vector<std::int64_t> ids;
  ids.reserve(ring.size());
  for (auto const& window : ring) {
    ids.push_back(static_cast<std::int64_t>(window.id().id()));
  }
  CatalogWindow::ptr active = catalog.ResolveMobileActiveWindow();
  std::int64_t const active_id = static_cast<std::int64_t>(active.id().id());
  reported_active_for_ui_ = active.id();
  ui_bridge_.PostMobilePages(ids, active_id);
}

}  // namespace apptraverse::example::surfaces
