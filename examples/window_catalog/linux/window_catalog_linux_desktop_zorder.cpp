#include "window_catalog_linux_desktop_zorder.h"

#include <unordered_map>

#include "apptraverse/platform/linux/linux_desktop_window_stack.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse::example::surfaces {
namespace {

ui::linux::LinuxDesktopWindowPresenter* CatalogWindowPresenter(
    CatalogWindow& window) {
  auto& presented = static_cast<ui::UiPresentedWindow&>(window);
  if (!presented.presenter.is_loaded()) {
    return nullptr;
  }
  return &static_cast<ui::linux::LinuxDesktopWindowPresenter&>(
      *presented.presenter);
}

void TryMapGtkWindow(CatalogWindow::ptr const& window_ref,
                     std::unordered_map<GtkWidget*, std::uint32_t>& out) {
  if (!window_ref.is_loaded()) {
    return;
  }
  ui::linux::LinuxDesktopWindowPresenter* presenter =
      CatalogWindowPresenter(*window_ref);
  if (presenter == nullptr) {
    return;
  }
  GtkWidget* const widget = presenter->TopLevelWindow();
  if (widget != nullptr) {
    out[widget] = window_ref.id().id();
  }
}

GtkWidget* GtkForCatalogWindowId(WindowCatalog& catalog,
                                   ae::ObjId::Type window_id) {
  if (catalog.persisted.main_window.id().id() == window_id) {
    ui::linux::LinuxDesktopWindowPresenter* presenter =
        CatalogWindowPresenter(*catalog.persisted.main_window);
    return presenter != nullptr ? presenter->TopLevelWindow() : nullptr;
  }
  catalog.persisted.list.Load();
  for (auto const& row : catalog.persisted.list->rows) {
    row.Load();
    auto* holder = static_cast<WindowHolder*>(&*row);
    if (!holder->window.is_loaded() || holder->window.id().id() != window_id) {
      continue;
    }
    ui::linux::LinuxDesktopWindowPresenter* presenter =
        CatalogWindowPresenter(*holder->window);
    return presenter != nullptr ? presenter->TopLevelWindow() : nullptr;
  }
  return nullptr;
}

}  // namespace

std::vector<std::uint32_t> CaptureCatalogDesktopZOrderTopToBottom(
    WindowCatalog& catalog) {
  std::unordered_map<GtkWidget*, std::uint32_t> widget_to_id;
  TryMapGtkWindow(catalog.persisted.main_window, widget_to_id);
  catalog.persisted.list.Load();
  for (auto const& row : catalog.persisted.list->rows) {
    row.Load();
    auto* holder = static_cast<WindowHolder*>(&*row);
    TryMapGtkWindow(holder->window, widget_to_id);
  }
  if (widget_to_id.empty()) {
    return {};
  }
  std::vector<GtkWidget*> widgets;
  widgets.reserve(widget_to_id.size());
  for (auto const& entry : widget_to_id) {
    widgets.push_back(entry.first);
  }
  std::vector<GtkWidget*> const stacked =
      ui::linux::CaptureDesktopStackingOrderTopToBottom(widgets);
  if (stacked.empty()) {
    return {};
  }
  std::vector<std::uint32_t> ordered_ids;
  ordered_ids.reserve(stacked.size());
  for (GtkWidget* widget : stacked) {
    ordered_ids.push_back(widget_to_id.at(widget));
  }
  return ordered_ids;
}

void RestoreCatalogDesktopZOrder(WindowCatalog& catalog) {
  if (catalog.persisted.window_order.empty()) {
    return;
  }
  std::vector<GtkWidget*> ordered_widgets;
  for (auto const& window_ref : catalog.persisted.window_order) {
    GtkWidget* const widget =
        GtkForCatalogWindowId(catalog, window_ref.id().id());
    if (widget != nullptr) {
      ordered_widgets.push_back(widget);
    }
  }
  ui::linux::ApplyDesktopStackingOrderTopToBottom(ordered_widgets);
}

void FlushCatalogNativeFrames(WindowCatalog& catalog) {
  auto flush_one = [](CatalogWindow::ptr const& window_ref) {
    if (!window_ref.is_loaded()) {
      return;
    }
    ui::linux::LinuxDesktopWindowPresenter* presenter =
        CatalogWindowPresenter(*window_ref);
    if (presenter != nullptr) {
      presenter->ReportNativeClientFrame();
    }
  };
  flush_one(catalog.persisted.main_window);
  catalog.persisted.list.Load();
  for (auto const& row : catalog.persisted.list->rows) {
    row.Load();
    auto* holder = static_cast<WindowHolder*>(&*row);
    flush_one(holder->window);
  }
}

}  // namespace apptraverse::example::surfaces
