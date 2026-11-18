#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <gtk/gtk.h>
#include "window_catalog_graph.h"
#include "window_catalog_linux_application.h"
#include "window_catalog_presenters_linux.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"

using namespace apptraverse;
namespace {
void Check(bool value, char const* why) {
  if (!value) { std::cerr << "CHECK failed: " << why << '\n'; std::abort(); }
}
using CatalogPresenter = example::surfaces::CatalogDesktopWindowPresenter;
struct Probe {
  WindowCatalog* catalog{};
  bool restore{};
  int stage{}, ticks{}, choice{}, allocations{}, stable_allocations{}, settle_ticks{};
  GtkWidget* first_window{};
  GdkRGBA regular_dark{}, light_popup{};
};
ui::UiAppearancePreferences Preference(int choice) {
  return {choice == 0 ? ui::UiThemeMode::kAuto : choice == 1 ? ui::UiThemeMode::kLight : ui::UiThemeMode::kDark,
          choice == 3};
}
void CheckRadio(CatalogPresenter& presenter, int selected) {
  for (int choice = 0; choice < 4; ++choice)
    Check(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(presenter.ThemeMenuItem(choice))) == (choice == selected),
          "native radio follows published appearance");
}
GdkRGBA Background(GtkWidget* widget) {
  GdkRGBA result{};
  gtk_style_context_get_background_color(gtk_widget_get_style_context(widget), GTK_STATE_FLAG_NORMAL, &result);
  return result;
}
void CheckCustomPalette(GtkWidget* window) {
  auto color = Background(window);
  Check(std::abs(color.red * 255 - 41) < 2 && std::abs(color.green * 255 - 50) < 2 &&
        std::abs(color.blue * 255 - 60) < 2, "custom dark native slate palette");
}
gboolean Tick(gpointer data) {
  auto& probe = *static_cast<Probe*>(data);
  Check(++probe.ticks < 600, "native menu/Add completed within 30 seconds");
  auto& catalog = *probe.catalog;
  auto& presenter = static_cast<CatalogPresenter&>(*catalog.persisted.main_window->presenter);
  auto* device = presenter.LocalDeviceState();
  if (probe.restore) {
    if (!gtk_widget_get_mapped(presenter.TopLevelWindow())) return G_SOURCE_CONTINUE;
    Check(device->appearance == Preference(3), "native LOCAL restored custom dark in a cold process");
    CheckRadio(presenter, 3);
    CheckCustomPalette(presenter.TopLevelWindow());
    CheckCustomPalette(gtk_widget_get_parent(presenter.ThemeMenuItem(0)));
    Check(catalog.persisted.list->rows.size() == 3, "cold USER retained native Add topology");
    Check(catalog.persisted.locale->language_index() == 1, "cold USER retained Russian locale");
    Check(std::string{gtk_menu_item_get_label(GTK_MENU_ITEM(presenter.ThemeMenuItem(0)))} == "Как в системе",
          "cold menu locale restored");
    std::cerr << "PASS: cold native menu, custom palette and separate LOCAL preference\n";
    gtk_window_close(GTK_WINDOW(presenter.TopLevelWindow()));
    return G_SOURCE_REMOVE;
  }
  if (probe.stage == 0) {
    if (!gtk_widget_get_mapped(presenter.TopLevelWindow())) return G_SOURCE_CONTINUE;
    Check(GTK_IS_MENU_BAR(presenter.PresentationMenu()), "visible native Settings menu");
    Check(gtk_widget_get_allocated_height(presenter.PresentationMenu()) > 1, "menu has a native allocation");
    Check(catalog.persisted.list->rows.empty(), "fresh native catalog");
    probe.choice = 1;
    ++probe.stage;
  }
  if (probe.stage == 1) {
    auto const before = device->appearance;
    gtk_menu_item_activate(GTK_MENU_ITEM(presenter.ThemeMenuItem(probe.choice)));
    Check(device->appearance == before, "menu did not mutate the UI mirror before publication");
    int const selected = before.theme == ui::UiThemeMode::kAuto ? 0 : before.theme == ui::UiThemeMode::kLight ? 1 : before.soften_dark ? 3 : 2;
    CheckRadio(presenter, selected);
    ++probe.stage;
  } else if (probe.stage == 2) {
    if (device->appearance != Preference(probe.choice)) return G_SOURCE_CONTINUE;
    CheckRadio(presenter, probe.choice);
    auto* popup = gtk_widget_get_parent(presenter.ThemeMenuItem(0));
    Check(GTK_IS_MENU(popup), "radio belongs to the native Settings popup");
    if (probe.choice == 1) probe.light_popup = Background(popup);
    if (probe.choice == 2) {
      probe.regular_dark = Background(presenter.TopLevelWindow());
      auto const dark_popup = Background(popup);
      Check(dark_popup.red + dark_popup.green + dark_popup.blue <
            probe.light_popup.red + probe.light_popup.green + probe.light_popup.blue,
            "native popup follows explicit Light and Dark");
      GdkRGBA foreground{};
      gtk_style_context_get_color(gtk_widget_get_style_context(popup), GTK_STATE_FLAG_NORMAL, &foreground);
      Check(foreground.red + foreground.green + foreground.blue >
            dark_popup.red + dark_popup.green + dark_popup.blue, "dark popup retains readable native text");
    }
    if (probe.choice == 3) {
      CheckCustomPalette(presenter.TopLevelWindow());
      CheckCustomPalette(popup);
      auto custom = Background(presenter.TopLevelWindow());
      Check(!gdk_rgba_equal(&custom, &probe.regular_dark), "stock and custom dark palettes differ");
      probe.choice = 0;
      probe.stage = 1;
    } else if (probe.choice == 0) {
      probe.choice = 3;
      probe.stage = 3;
      gtk_menu_item_activate(GTK_MENU_ITEM(presenter.ThemeMenuItem(3)));
    } else {
      ++probe.choice;
      probe.stage = 1;
    }
  } else if (probe.stage == 3) {
    if (device->appearance != Preference(3)) return G_SOURCE_CONTINUE;
    auto& add = static_cast<ui::linux::LinuxPushButtonPresenter&>(*catalog.persisted.presenters.add_button_presenter);
    gtk_button_clicked(GTK_BUTTON(add.ButtonWidget()));
    ++probe.stage;
  } else if (probe.stage >= 4 && probe.stage <= 6) {
    std::size_t const wanted = static_cast<std::size_t>(probe.stage - 3);
    if (catalog.persisted.list->rows.size() != wanted) return G_SOURCE_CONTINUE;
    auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows.back());
    auto& child = static_cast<ui::linux::LinuxDesktopWindowPresenter&>(*holder.window->presenter);
    if (!gtk_widget_get_mapped(child.TopLevelWindow())) return G_SOURCE_CONTINUE;
    CheckCustomPalette(child.TopLevelWindow());
    if (wanted == 1) probe.first_window = child.TopLevelWindow();
    else Check(gtk_widget_get_mapped(probe.first_window), "surviving child retains its native widget");
    if (wanted < 3) {
      auto& add = static_cast<ui::linux::LinuxPushButtonPresenter&>(*catalog.persisted.presenters.add_button_presenter);
      gtk_button_clicked(GTK_BUTTON(add.ButtonWidget()));
    } else {
      auto& language = static_cast<ui::linux::LinuxPushButtonPresenter&>(*catalog.persisted.presenters.language_button_presenter);
      gtk_button_clicked(GTK_BUTTON(language.ButtonWidget()));
    }
    ++probe.stage;
  } else if (probe.stage == 7) {
    if (catalog.persisted.locale->language_index() != 1) return G_SOURCE_CONTINUE;
    CheckRadio(presenter, 3);
    Check(std::string{gtk_menu_item_get_label(GTK_MENU_ITEM(presenter.ThemeMenuItem(3)))} == "Своя тёмная",
          "native Settings choices follow Russian locale");
    presenter.model_proxy->Invoke(catalog.persisted.main_window.id(), &ui::UiDesktopWindow::RequestFrame,
                                  20, 20, 600, 320);
    ++probe.stage;
  } else if (probe.stage == 8) {
    if (gtk_widget_get_allocated_width(presenter.ClientViewport()) != 600 ||
        gtk_widget_get_allocated_height(presenter.ClientViewport()) != 320) return G_SOURCE_CONTINUE;
    g_signal_connect(presenter.ClientMount(), "size-allocate", G_CALLBACK(+[](GtkWidget*, GtkAllocation*, gpointer value) {
      ++static_cast<Probe*>(value)->allocations;
    }), &probe);
    probe.settle_ticks = 0;
    ++probe.stage;
  } else if (probe.stage == 9) {
    if (++probe.settle_ticks < 6) return G_SOURCE_CONTINUE;
    probe.stable_allocations = probe.allocations;
    // Visits the same native boundary as an unrelated publication. No model
    // mutation or publication suppression is used to hide a resize cycle.
    for (int repeat = 0; repeat < 3; ++repeat) {
      presenter.OnModelChanged();
      catalog.persisted.presenters.list_presenter->OnModelChanged();
      catalog.persisted.presenters.add_button_presenter->OnModelChanged();
    }
    probe.settle_ticks = 0;
    ++probe.stage;
  } else if (probe.stage == 10) {
    if (++probe.settle_ticks < 6) return G_SOURCE_CONTINUE;
    Check(probe.allocations == probe.stable_allocations, "unchanged presentation does not restart GTK allocation");
    CheckCustomPalette(presenter.TopLevelWindow());
    CheckRadio(presenter, 3);
    std::cerr << "PASS: four published native menu choices, three Add actions, child palette, locale, menu-aware content size and quiet allocation\n";
    gtk_window_close(GTK_WINDOW(presenter.TopLevelWindow()));
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}
}
int main(int argc, char** argv) {
  Check(argc >= 2, "isolated profile argument");
  EnsureWindowCatalogGraphRegistration();
  std::filesystem::path profile = argv[1];
  WindowCatalogLinuxApplication app{profile};
  Probe probe;
  probe.restore = argc >= 3;
  auto callbacks = app.MakeHostCallbacks();
  auto after_initial = std::move(callbacks.after_initial);
  callbacks.after_initial = [&](ae::Obj& root) {
    after_initial(root);
    probe.catalog = &static_cast<WindowCatalog&>(root);
    g_timeout_add(50, Tick, &probe);
  };
  Check(app.Run(std::move(callbacks)) == 0, "native application shutdown");
  Check(std::filesystem::file_size(profile / "user.bin") > 0 &&
        std::filesystem::file_size(profile / "local" / "user.bin") > 0, "native USER/LOCAL persisted separately");
}
