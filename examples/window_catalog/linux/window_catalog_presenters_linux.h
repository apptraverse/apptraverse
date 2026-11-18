#ifndef APPTRAVERSE_LINUX_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_LINUX_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_list_presenter_mixin.h"
#include <cassert>
#include <array>
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/linux/linux_ui_list_presenter.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"

namespace apptraverse::example::surfaces {

class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::linux::LinuxDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter, ui::linux::LinuxDesktopWindowPresenter, 0)

 protected:
  CatalogDesktopWindowPresenter() = default;

 public:
  explicit CatalogDesktopWindowPresenter(ae::ObjProp prop)
      : WindowCatalogDesktopPresenterMixin{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  void OnLoad() override {
    assert(SessionMainNativeFrameQueue() != nullptr);
    assert(session_catalog_main_window_id_.is_valid());
    ui::linux::LinuxDesktopWindowPresenter::OnLoad();
    SyncPresentationMenu();
  }

  GtkWidget* CreatePresentationMenu() override {
    if (window.id() != catalog->persisted.main_window.id()) return nullptr;
    auto* bar = gtk_menu_bar_new();
    settings_item_ = gtk_menu_item_new_with_label("Settings");
    auto* menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(settings_item_), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), settings_item_);
    GSList* group = nullptr;
    for (std::size_t index = 0; index < theme_items_.size(); ++index) {
      auto* item = gtk_radio_menu_item_new_with_label(group, "");
      group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
      theme_items_[index] = item;
      g_object_set_data(G_OBJECT(item), "apptraverse-theme-choice", GUINT_TO_POINTER(index + 1));
      gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    for (auto* item : theme_items_)
      g_signal_connect(item, "toggled", G_CALLBACK(OnThemeToggled), this);
    SyncPresentationMenu();
    gtk_widget_show_all(bar);
    return bar;
  }

  void OnModelChanged() override {
    ui::linux::LinuxDesktopWindowPresenter::OnModelChanged();
    SyncPresentationMenu();
  }

  void OnUnload() override {
    ui::linux::LinuxDesktopWindowPresenter::OnUnload();
    settings_item_ = nullptr;
    theme_items_.fill(nullptr);
  }

  GtkWidget* ThemeMenuItem(std::size_t choice) const { return theme_items_.at(choice); }

  void ReportNativeFrameToModel(std::int32_t x, std::int32_t y,
                                std::int32_t client_width,
                                std::int32_t client_height,
                                bool updates_restore_geometry) override {
    ::apptraverse::WindowCatalogMainNativeFrameQueue* const queue =
        SessionMainNativeFrameQueue();
    if (this->window.id() == session_catalog_main_window_id_) {
      assert(queue != nullptr);
      assert(session_catalog_obj_id_.is_valid());
      ::apptraverse::PostWindowCatalogMainNativeFrame(
          *queue, this->model_proxy, session_catalog_obj_id_, this->window.id(),
          x, y, client_width, client_height, updates_restore_geometry);
      return;
    }
    this->model_proxy->Invoke(
        catalog.id(), &WindowCatalog::SubmitHolderChildWindowFrameFromUi,
        this->window.id().id(), x, y, client_width, client_height,
        updates_restore_geometry);
  }

 private:
  static void OnThemeToggled(GtkCheckMenuItem* item, gpointer data) {
    auto& self = *static_cast<CatalogDesktopWindowPresenter*>(data);
    if (self.syncing_menu_ || !gtk_check_menu_item_get_active(item)) return;
    auto const choice = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "apptraverse-theme-choice")) - 1;
    ui::UiAppearancePreferences preference;
    preference.theme = choice == 0 ? ui::UiThemeMode::kAuto :
        choice == 1 ? ui::UiThemeMode::kLight : ui::UiThemeMode::kDark;
    preference.soften_dark = choice == 3;
    // GTK changes a native radio before this signal. Keep its visible check
    // on the published state until the queued model Event is acknowledged.
    self.SyncPresentationMenu();
    self.model_proxy->Invoke(self.catalog->persisted.device_state.id(),
        &ui::UiDeviceState::SetAppearance, preference);
  }

  void SyncPresentationMenu() {
    if (!settings_item_) return; // Only the main window composes a native menu.
    syncing_menu_ = true;
    bool const russian = catalog->persisted.locale->language_index() == 1;
    auto set_label = [](GtkWidget* item, char const* text) {
      if (std::string{gtk_menu_item_get_label(GTK_MENU_ITEM(item))} != text)
        gtk_menu_item_set_label(GTK_MENU_ITEM(item), text);
    };
    set_label(settings_item_, russian ? "Настройки" : "Settings");
    char const* labels[] = {russian ? "Как в системе" : "From system",
        russian ? "Светлая" : "Light", russian ? "Тёмная" : "Dark",
        russian ? "Своя тёмная" : "Custom dark"};
    auto const preference = catalog->persisted.device_state->appearance;
    std::size_t const selected = preference.theme == ui::UiThemeMode::kAuto ? 0 :
        preference.theme == ui::UiThemeMode::kLight ? 1 : preference.soften_dark ? 3 : 2;
    for (std::size_t index = 0; index < theme_items_.size(); ++index) {
      set_label(theme_items_[index], labels[index]);
      if (index == selected && !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(theme_items_[index])))
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(theme_items_[index]), TRUE);
    }
    syncing_menu_ = false;
  }
  GtkWidget* settings_item_{nullptr};
  std::array<GtkWidget*, 4> theme_items_{};
  bool syncing_menu_{false};
};

class CatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<
          ui::linux::LinuxListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      CatalogListContainerPresenter, ui::linux::LinuxListContainerPresenter, 0)

 protected:
  CatalogListContainerPresenter() = default;

 public:
  explicit CatalogListContainerPresenter(ae::ObjProp prop)
      : WindowCatalogListPresenterMixin{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override;
  void SubmitListRowActivatedFromUi(std::size_t row_index) override;
};


}  // namespace apptraverse::example::surfaces

#endif
