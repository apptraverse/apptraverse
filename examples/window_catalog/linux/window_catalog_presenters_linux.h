#ifndef APPTRAVERSE_LINUX_WINDOW_CATALOG_PRESENTERS_H_
#define APPTRAVERSE_LINUX_WINDOW_CATALOG_PRESENTERS_H_

#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_list_presenter_mixin.h"
#include <cassert>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>
#include "window_catalog_menu.h"
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
    SetPresentationFullscreen(UiState().IsWindowFullscreen(window.id()));
    if (window.id() == catalog->persisted.main_window.id()) {
      layout_menu_ = gtk_menu_new();
      gtk_menu_attach_to_widget(GTK_MENU(layout_menu_), TopLevelWindow(), nullptr);
      auto const& items = UiState().menu->layout_items;
      for (unsigned index = 0; index < items.size(); ++index) {
        if (items[index].action == CatalogMenuAction::kResetLayout)
          gtk_menu_shell_append(GTK_MENU_SHELL(layout_menu_), gtk_separator_menu_item_new());
        auto* item = gtk_check_menu_item_new_with_label("");
        gtk_menu_shell_append(GTK_MENU_SHELL(layout_menu_), item);
        layout_items_.push_back(item);
        g_object_set_data(G_OBJECT(item), "apptraverse-layout-item", GUINT_TO_POINTER(index));
        g_signal_connect(item, "activate", G_CALLBACK(OnLayoutActivated), this);
      }
      gtk_widget_show_all(layout_menu_);
    }
    SyncPresentationMenu();
  }

  bool HasFullscreenAction() const override { return true; }
  ui::UiLayoutNode const* FullscreenActionLayout() const override {
    return &UiState().DisplayMenu().fullscreen_toolbar;
  }
  std::string ResolveFullscreenActionText() const override {
    return UiState().DisplayMenu().fullscreen_items[
        UiState().IsWindowFullscreen(window.id()) ? 1 : 0].label->Resolve(locale->language_index());
  }
  void ToggleFullscreenFromUi() override {
    // Same-size Wayland output moves need not emit a configure event. Queue
    // their native observation before the fullscreen intent on the same proxy.
    ReportNativeClientFrame();
    model_proxy->Invoke(UiState().obj_id, &CatalogUiState::ToggleWindowFullscreenFromUi, window.id());
  }
  void ExitFullscreenFromUi() override {
    ReportNativeClientFrame();
    model_proxy->Invoke(UiState().obj_id, &CatalogUiState::RequestWindowFullscreenFromUi, window.id(), false);
  }
  bool ShowPresentationContextMenu(GdkEvent* event) override {
    if (!layout_menu_) return false;
    SyncPresentationMenu();
    if (event) gtk_menu_popup_at_pointer(GTK_MENU(layout_menu_), event);
    else gtk_menu_popup_at_widget(GTK_MENU(layout_menu_), TopLevelWindow(), GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, nullptr);
    return true;
  }
  GtkWidget* LayoutMenuItem(unsigned index) const { return layout_items_.at(index); }

  GtkWidget* CreatePresentationMenu() override {
    if (window.id() != catalog->persisted.main_window.id()) return nullptr;
    auto* bar = gtk_menu_bar_new();
    auto const& groups = UiState().menu->groups;
    for (unsigned group_index = 0; group_index < groups.size(); ++group_index) {
      NativeGroup native;
      native.heading = gtk_menu_item_new_with_mnemonic("");
      auto* popup = gtk_menu_new();
      gtk_menu_item_set_submenu(GTK_MENU_ITEM(native.heading), popup);
      gtk_menu_shell_append(GTK_MENU_SHELL(bar), native.heading);
      GSList* radio_group = nullptr;
      for (unsigned item_index = 0; item_index < groups[group_index].items.size(); ++item_index) {
        auto* item = gtk_radio_menu_item_new_with_label(radio_group, "");
        radio_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        native.items.push_back(item);
        g_object_set_data(G_OBJECT(item), "apptraverse-menu-group", GUINT_TO_POINTER(group_index));
        g_object_set_data(G_OBJECT(item), "apptraverse-menu-item", GUINT_TO_POINTER(item_index));
        gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);
      }
      menu_groups_.push_back(std::move(native));
    }
    for (auto const& group : menu_groups_)
      for (auto* item : group.items)
        g_signal_connect(item, "toggled", G_CALLBACK(OnMenuToggled), this);
    SyncPresentationMenu();
    gtk_widget_show_all(bar);
    return bar;
  }

  void OnModelChanged() override {
    SetPresentationFullscreen(UiState().IsWindowFullscreen(window.id()));
    ui::linux::LinuxDesktopWindowPresenter::OnModelChanged();
    SyncPresentationMenu();
  }

  void OnUnload() override {
    if (layout_menu_) { gtk_widget_destroy(layout_menu_); layout_menu_ = nullptr; }
    layout_items_.clear();
    ui::linux::LinuxDesktopWindowPresenter::OnUnload();
    menu_groups_.clear();
  }

  GtkWidget* MenuItem(CatalogMenuAction action, int value) const {
    auto const& groups = UiState().menu->groups;
    for (unsigned group = 0; group < groups.size(); ++group)
      for (unsigned item = 0; item < groups[group].items.size(); ++item)
        if (groups[group].items[item].action == action && groups[group].items[item].value == value)
          return menu_groups_.at(group).items.at(item);
    std::abort(); // Required authored command is absent.
  }
  GtkWidget* ThemeMenuItem(std::size_t value) const {
    return MenuItem(CatalogMenuAction::kTheme, static_cast<int>(value));
  }
  GtkWidget* LanguageMenuItem(int value) const {
    return MenuItem(CatalogMenuAction::kLanguage, value);
  }

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
  CatalogUiState const& UiState() const {
    return static_cast<CatalogUiState const&>(*LocalDeviceState());
  }
  static std::string NativeLabel(std::string const& model_text) {
    // The shared menu uses '&' mnemonics; GTK spells the same marker '_'.
    std::string result;
    for (std::size_t index = 0; index < model_text.size(); ++index) {
      char const c = model_text[index];
      if (c == '&') {
        if (index + 1 < model_text.size() && model_text[index + 1] == '&') {
          result += '&'; ++index;
        } else result += '_';
      } else {
        if (c == '_') result += '_';
        result += c;
      }
    }
    return result;
  }
  static void OnMenuToggled(GtkCheckMenuItem* item, gpointer data) {
    auto& self = *static_cast<CatalogDesktopWindowPresenter*>(data);
    if (self.syncing_menu_ || !gtk_check_menu_item_get_active(item)) return;
    auto const group = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "apptraverse-menu-group"));
    auto const index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "apptraverse-menu-item"));
    // Native activation is intent only: restore the published check until the
    // queued model Event returns through the UI mirror.
    self.SyncPresentationMenu();
    self.model_proxy->Invoke(self.LocalDeviceState()->obj_id,
        &CatalogUiState::ExecuteMenuCommand, group, index);
  }

  static void OnLayoutActivated(GtkMenuItem* item, gpointer data) {
    auto& self = *static_cast<CatalogDesktopWindowPresenter*>(data);
    if (self.syncing_menu_) return;
    auto const index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "apptraverse-layout-item"));
    self.SyncPresentationMenu();
    self.model_proxy->Invoke(self.UiState().obj_id, &CatalogUiState::ExecuteLayoutCommand, index);
  }

  void SyncPresentationMenu() {
    if (menu_groups_.empty()) return; // Child windows compose no application menu.
    syncing_menu_ = true;
    auto const& state = UiState();
    int const language = catalog->persisted.locale->language_index();
    auto set_label = [](GtkWidget* item, std::string const& text, bool mnemonic) {
      if (std::string{gtk_menu_item_get_label(GTK_MENU_ITEM(item))} != text)
        gtk_menu_item_set_label(GTK_MENU_ITEM(item), text.c_str());
      if (gtk_menu_item_get_use_underline(GTK_MENU_ITEM(item)) != mnemonic)
        gtk_menu_item_set_use_underline(GTK_MENU_ITEM(item), mnemonic);
    };
    for (unsigned group = 0; group < menu_groups_.size(); ++group) {
      auto const& definition = state.menu->groups[group];
      auto const& native = menu_groups_[group];
      set_label(native.heading, NativeLabel(definition.label->Resolve(language)), true);
      for (unsigned index = 0; index < native.items.size(); ++index) {
        auto* item = native.items[index];
        auto const& command = definition.items[index];
        set_label(item, command.label->Resolve(language), false);
        if (state.MenuItemSelected(command) && !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
          gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
      }
    }
    for (unsigned index = 0; index < layout_items_.size(); ++index) {
      auto* item = layout_items_[index];
      auto const& command = state.menu->layout_items[index];
      set_label(item, command.label->Resolve(language), false);
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), state.MenuItemSelected(command));
    }
    syncing_menu_ = false;
  }
  GtkWidget* layout_menu_{nullptr};
  std::vector<GtkWidget*> layout_items_;
  struct NativeGroup { GtkWidget* heading{}; std::vector<GtkWidget*> items; };
  std::vector<NativeGroup> menu_groups_;
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
