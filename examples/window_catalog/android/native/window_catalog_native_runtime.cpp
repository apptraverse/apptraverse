#include "window_catalog_native_runtime.h"

#include "window_catalog_android_lifecycle.h"
#include "window_catalog_android_persistence.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_menu.h"

#include "android_jni_util.h"

#include "apptraverse/platform/android/android_ui_window_presenter.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse::example::surfaces {
namespace {
using ui::android::CheckedJni;
constexpr int kFullscreenCommand = 1000;
constexpr int kFirstLayoutCommand = 1100;

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
  jclass cls = CheckedJni(env, "visibility class", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  jmethodID method = CheckedJni(env, "visibility method", [&] { return env->GetStaticMethodID(cls,
      "setClientMountVisible", "(Landroid/widget/FrameLayout;Z)V"); });
  CheckedJni(env, "page visibility", [&] { env->CallStaticVoidMethod(cls, method, mount, visible ? JNI_TRUE : JNI_FALSE); });
  env->DeleteLocalRef(cls);
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
    std::filesystem::path state_dir, std::filesystem::path local_seed_dir,
    android::WindowCatalogUiBridge ui_bridge,
    android::AndroidUiBridge android_ui_bridge)
    : state_dir_{std::move(state_dir)},
      ui_bridge_{std::move(ui_bridge)},
      android_ui_bridge_{std::move(android_ui_bridge)},
      profile_{state_dir_, local_seed_dir} {}

WindowCatalogNativeRuntime::~WindowCatalogNativeRuntime() {
  if (host_) {
    host_->JoinModelThreadForRelease();
  }
}

void WindowCatalogNativeRuntime::SetPresentationRoot(JNIEnv* env,
                                                      jobject activity,
                                                      jobject content_root) {
  if (!host_) {
    startup_environment_ = ui::android::CaptureAndroidDeviceEnvironment(env, activity);
    android::AndroidApplicationHostCallbacks callbacks;
    callbacks.run_model = [this](ApplicationPublishedCallback on_published) {
      return RunWindowCatalogAndroidModel(profile_, runtime_,
                                          std::move(on_published), startup_environment_);
    };
    callbacks.after_initial = [this](ae::Obj& root) {
      SyncMobilePagerChrome(CatalogRoot(root));
      PublishMobilePages();
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

void WindowCatalogNativeRuntime::ClearPresentationRoot(JNIEnv* env, jobject activity) {
  if (host_) host_->ClearPresentationRoot(env, activity);
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

void WindowCatalogNativeRuntime::PopulateMenu(JNIEnv* env, jobject menu) {
  // The Activity can request its menu before the initial publication. It has
  // no semantic menu until the required authored graph reaches the UI mirror.
  if (!host_ || !host_->ui_session()) return;
  auto& catalog = CatalogRoot(host_->ui_session()->root());
  auto const& state = static_cast<CatalogUiState const&>(*catalog.persisted.device_state);
  int const language = catalog.persisted.locale->language_index();
  auto* menu_class = CheckedJni(env, "menu class", [&] { return env->FindClass("android/view/Menu"); });
  auto* item_class = CheckedJni(env, "menu item class", [&] { return env->FindClass("android/view/MenuItem"); });
  auto add_submenu = CheckedJni(env, "add submenu", [&] { return env->GetMethodID(menu_class, "addSubMenu", "(Ljava/lang/CharSequence;)Landroid/view/SubMenu;"); });
  auto add_item = CheckedJni(env, "add menu item", [&] { return env->GetMethodID(menu_class, "add", "(IIILjava/lang/CharSequence;)Landroid/view/MenuItem;"); });
  auto checkable = CheckedJni(env, "checkable menu group", [&] { return env->GetMethodID(menu_class, "setGroupCheckable", "(IZZ)V"); });
  auto checked = CheckedJni(env, "checked menu item", [&] { return env->GetMethodID(item_class, "setChecked", "(Z)Landroid/view/MenuItem;"); });
  auto set_checkable = CheckedJni(env, "checkable menu item", [&] { return env->GetMethodID(item_class, "setCheckable", "(Z)Landroid/view/MenuItem;"); });
  auto caption = [&](ui::UiLocalizationString const& text) {
    // Android has no menu mnemonic marker. Preserve escaped literal '&'.
    std::string result;
    auto const source = text.Resolve(language);
    for (std::size_t index = 0; index < source.size(); ++index) {
      if (source[index] != '&') result += source[index];
      else if (index + 1 < source.size() && source[index + 1] == '&') { result += '&'; ++index; }
    }
    return ui::android::Utf8ToJstring(env, result.c_str());
  };
  int native_id = 1;
  for (auto const& group : state.menu->groups) {
    auto title = caption(*group.label);
    auto submenu = CheckedJni(env, "create authored submenu", [&] { return env->CallObjectMethod(menu, add_submenu, title); });
    env->DeleteLocalRef(title);
    for (auto const& item : group.items) {
      title = caption(*item.label);
      auto native_item = CheckedJni(env, "create authored item", [&] { return env->CallObjectMethod(submenu, add_item, 1, native_id++, 0, title); });
      auto result = CheckedJni(env, "project published menu check", [&] { return env->CallObjectMethod(native_item, checked, state.MenuItemSelected(item) ? JNI_TRUE : JNI_FALSE); });
      env->DeleteLocalRef(result);
      env->DeleteLocalRef(native_item);
      env->DeleteLocalRef(title);
    }
    CheckedJni(env, "project native radio group", [&] { env->CallVoidMethod(submenu, checkable, 1, JNI_TRUE, JNI_TRUE); });
    env->DeleteLocalRef(submenu);
  }
  auto active_id = catalog.ResolveActiveWindow().id();
  menu_window_id_ = active_id;
  menu_fullscreen_intent_ = !state.IsWindowFullscreen(active_id);
  auto title = caption(*state.DisplayMenu().fullscreen_items[
      state.IsWindowFullscreen(active_id) ? 1 : 0].label);
  auto native_item = CheckedJni(env, "create fullscreen item", [&] {
    return env->CallObjectMethod(menu, add_item, 0, kFullscreenCommand, 0, title);
  });
  env->DeleteLocalRef(native_item); env->DeleteLocalRef(title);
  // Layout is a main-window semantic override, offered in the native touch menu.
  if (active_id == catalog.persisted.main_window.id()) {
    auto const& items = state.menu->layout_items;
    for (unsigned index = 0; index < items.size(); ++index) {
      auto const& item = items[index];
      title = caption(*item.label);
      native_item = CheckedJni(env, "create layout item", [&] {
        return env->CallObjectMethod(menu, add_item, 2, kFirstLayoutCommand + index, 0, title);
      });
      auto result = CheckedJni(env, "layout item checkable", [&] {
        return env->CallObjectMethod(native_item, set_checkable,
            item.action == CatalogMenuAction::kLayoutFlow ? JNI_TRUE : JNI_FALSE);
      });
      env->DeleteLocalRef(result);
      result = CheckedJni(env, "project published layout check", [&] {
        return env->CallObjectMethod(native_item, checked, state.MenuItemSelected(item) ? JNI_TRUE : JNI_FALSE);
      });
      env->DeleteLocalRef(result); env->DeleteLocalRef(native_item); env->DeleteLocalRef(title);
    }
  }
  env->DeleteLocalRef(item_class);
  env->DeleteLocalRef(menu_class);
}

void WindowCatalogNativeRuntime::ExecuteMenuCommand(int native_item_id) {
  if (!host_ || !host_->ui_session()) return;
  auto& catalog = CatalogRoot(host_->ui_session()->root());
  auto const& state = static_cast<CatalogUiState const&>(*catalog.persisted.device_state);
  auto active_id = catalog.ResolveActiveWindow().id();
  if (native_item_id == kFullscreenCommand) {
    host_->model_proxy().Invoke(state.obj_id, &CatalogUiState::RequestWindowFullscreenFromUi,
        menu_window_id_, menu_fullscreen_intent_);
    return;
  }
  if (native_item_id >= kFirstLayoutCommand &&
      native_item_id < kFirstLayoutCommand + static_cast<int>(state.menu->layout_items.size())) {
    if (active_id == catalog.persisted.main_window.id())
      host_->model_proxy().Invoke(state.obj_id, &CatalogUiState::ExecuteLayoutCommand,
          static_cast<unsigned>(native_item_id - kFirstLayoutCommand));
    return;
  }
  int token = 1;
  for (unsigned group = 0; group < state.menu->groups.size(); ++group)
    for (unsigned item = 0; item < state.menu->groups[group].items.size(); ++item, ++token)
      if (token == native_item_id) {
        host_->model_proxy().Invoke(state.obj_id, &CatalogUiState::ExecuteMenuCommand, group, item);
        return;
      }
}

void WindowCatalogNativeRuntime::ExitFullscreen() {
  if (!host_ || !host_->ui_session()) return;
  auto& catalog = CatalogRoot(host_->ui_session()->root());
  auto const& state = static_cast<CatalogUiState const&>(*catalog.persisted.device_state);
  host_->model_proxy().Invoke(state.obj_id, &CatalogUiState::RequestWindowFullscreenFromUi,
      catalog.ResolveActiveWindow().id(), false);
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
  auto const& state = static_cast<CatalogUiState const&>(*catalog.persisted.device_state);
  JNIEnv* env = ui::android::RequireUiThreadEnv(presentation_host_);
  auto cls = CheckedJni(env, "Activity class", [&] { return env->GetObjectClass(presentation_host_.activity); });
  auto method = CheckedJni(env, "published fullscreen method", [&] {
    return env->GetMethodID(cls, "setPresentedFullscreen", "(Z)V");
  });
  CheckedJni(env, "apply published fullscreen", [&] {
    env->CallVoidMethod(presentation_host_.activity, method,
        state.IsWindowFullscreen(active_id) ? JNI_TRUE : JNI_FALSE);
  });
  env->DeleteLocalRef(cls);
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
