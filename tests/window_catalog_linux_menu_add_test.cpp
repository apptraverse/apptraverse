#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
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
  bool restore{}, display_only{};
  int stage{}, ticks{}, choice{}, allocations{}, stable_allocations{}, settle_ticks{};
  GtkWidget* first_window{};
  GdkRGBA regular_dark{}, light_popup{};
  ui::UiDisplayRect normal_frame{};
  ae::ObjId fullscreen_child;
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
bool NativeFullscreen(CatalogPresenter& presenter) {
  return (gdk_window_get_state(gtk_widget_get_window(presenter.TopLevelWindow())) & GDK_WINDOW_STATE_FULLSCREEN) != 0;
}
void Key(CatalogPresenter& presenter, guint key) {
  GdkEventKey event{}; event.type = GDK_KEY_PRESS; event.keyval = key;
  gboolean handled = FALSE;
  g_signal_emit_by_name(presenter.TopLevelWindow(), "key-press-event", &event, &handled);
  Check(handled, "native window handled fullscreen key");
}
gboolean Tick(gpointer data) {
  auto& probe = *static_cast<Probe*>(data);
  ++probe.ticks;
  auto& catalog = *probe.catalog;
  auto& presenter = static_cast<CatalogPresenter&>(*catalog.persisted.main_window->presenter);
  auto* device = presenter.LocalDeviceState();
  if (probe.ticks >= 600) {
    std::cerr << "GTK menu timeout stage=" << probe.stage << " viewport="
        << gtk_widget_get_allocated_width(presenter.ClientViewport()) << 'x'
        << gtk_widget_get_allocated_height(presenter.ClientViewport())
        << " fullscreen=" << NativeFullscreen(presenter) << " restore=" << probe.restore << '\n';
    Check(false, "native menu/Add completed within 30 seconds");
  }
  if (probe.restore) {
    if (!gtk_widget_get_mapped(presenter.TopLevelWindow())) return G_SOURCE_CONTINUE;
    Check(device->appearance == Preference(3), "native LOCAL restored custom dark in a cold process");
    CheckRadio(presenter, 3);
    CheckCustomPalette(presenter.TopLevelWindow());
    CheckCustomPalette(gtk_widget_get_parent(presenter.ThemeMenuItem(0)));
    Check(catalog.persisted.list->rows.size() == 3, "cold USER retained native Add topology");
    Check(catalog.persisted.locale->language_index() == 1, "cold LOCAL retained Russian locale");
    Check(std::string{gtk_menu_item_get_label(GTK_MENU_ITEM(presenter.ThemeMenuItem(0)))} == "Из системы",
          "cold menu locale restored");
    auto& state = static_cast<CatalogUiState&>(*device);
    if (!state.IsWindowFullscreen(presenter.window.id()) || !NativeFullscreen(presenter)) return G_SOURCE_CONTINUE;
    Check(std::string{gtk_button_get_label(GTK_BUTTON(presenter.FullscreenAction()))}.find("Esc") != std::string::npos,
          "cold fullscreen exit remains localized and accessible");
    Check(device->PlacementFor(presenter.window.id())->frame_dip.width < gdk_window_get_width(gtk_widget_get_window(presenter.TopLevelWindow())),
          "cold fullscreen retained a separate normal placement");
    std::cerr << "PASS: cold native menu, fullscreen, custom palette and separate LOCAL preference\n";
    gtk_window_close(GTK_WINDOW(presenter.TopLevelWindow()));
    return G_SOURCE_REMOVE;
  }
  if (probe.stage == 0) {
    if (!gtk_widget_get_mapped(presenter.TopLevelWindow())) return G_SOURCE_CONTINUE;
    Check(GTK_IS_MENU_BAR(presenter.PresentationMenu()), "visible native Theme/Language menu");
    Check(gtk_widget_get_allocated_height(presenter.PresentationMenu()) > 1, "menu has a native allocation");
    Check(catalog.persisted.list->rows.empty(), "fresh native catalog");
    Check(!catalog.persisted.texts.label_language.id().is_valid() &&
          !catalog.persisted.presenters.language_button_presenter.id().is_valid() &&
          !catalog.persisted.buttons.language_button.id().is_valid(),
          "menu-only supply omits the language content control and presenter");
    Check(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(presenter.LanguageMenuItem(0))),
          "published English menu check");
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
    Check(GTK_IS_MENU(popup), "radio belongs to the native Theme/Language popup");
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
      auto const before = catalog.persisted.locale->language_index();
      gtk_menu_item_activate(GTK_MENU_ITEM(presenter.LanguageMenuItem(1)));
      Check(catalog.persisted.locale->language_index() == before,
            "language input does not mutate UI mirror before publication");
    }
    ++probe.stage;
  } else if (probe.stage == 7) {
    if (catalog.persisted.locale->language_index() != 1) return G_SOURCE_CONTINUE;
    CheckRadio(presenter, 3);
    Check(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(presenter.LanguageMenuItem(1))),
          "published Russian menu check");
    Check(std::string{gtk_menu_item_get_label(GTK_MENU_ITEM(presenter.ThemeMenuItem(3)))} == "Мягкая тёмная",
          "native Theme/Language choices follow Russian locale");
    presenter.model_proxy->Invoke(catalog.persisted.main_window.id(), &ui::UiDesktopWindow::RequestFrame,
                                  20, 20, 1, 1);
    probe.stage = 71;
  } else if (probe.stage == 71) {
    if (catalog.persisted.main_window->restore_client_width() != 1 || gtk_widget_get_allocated_width(presenter.ClientViewport()) <= 1) return G_SOURCE_CONTINUE;
    // GTK's authored menu enforces a genuine native minimum. A constrained
    // explicit request must not suppress the following ordinary user resize.
    gtk_window_resize(GTK_WINDOW(presenter.TopLevelWindow()), 620, 360 + presenter.NativeChromeHeight());
    probe.stage = 72;
  } else if (probe.stage == 72) {
    if (gtk_widget_get_allocated_width(presenter.ClientViewport()) != 620 || gtk_widget_get_allocated_height(presenter.ClientViewport()) != 360) return G_SOURCE_CONTINUE;
    GdkRectangle outer{}; gdk_window_get_frame_extents(gtk_widget_get_window(presenter.TopLevelWindow()), &outer);
    auto const* placement = device->PlacementFor(presenter.window.id());
    if (!placement || placement->frame_dip.width != outer.width || placement->frame_dip.height != outer.height) return G_SOURCE_CONTINUE;
    Check(catalog.persisted.main_window->restore_client_width() == 1, "constrained native request and user resize remain LOCAL");
    presenter.model_proxy->Invoke(catalog.persisted.main_window.id(), &ui::UiDesktopWindow::RequestFrame,
                                  20, 20, 600, 320);
    probe.stage = 8;
  } else if (probe.stage == 8) {
    if (gtk_widget_get_allocated_width(presenter.ClientViewport()) != 600 ||
        gtk_widget_get_allocated_height(presenter.ClientViewport()) != 320) return G_SOURCE_CONTINUE;
    if (probe.display_only) {
      // Separate display acceptance does not make a no-op allocation claim.
      // The ordinary scenario retains its existing quiet-allocation assertion.
      gtk_menu_item_activate(GTK_MENU_ITEM(presenter.LayoutMenuItem(2)));
      probe.stage = 11;
      return G_SOURCE_CONTINUE;
    }
    g_signal_connect(presenter.ClientMount(), "size-allocate", G_CALLBACK(+[](GtkWidget*, GtkAllocation*, gpointer value) {
      ++static_cast<Probe*>(value)->allocations;
    }), &probe);
    probe.settle_ticks = 0;
    ++probe.stage;
  } else if (probe.stage == 9) {
    // A busy native loop may still be completing earlier resize/style work.
    // Establish a quiet allocation boundary before measuring the no-op calls.
    if (probe.allocations != probe.stable_allocations) {
      probe.stable_allocations = probe.allocations;
      probe.settle_ticks = 0;
    }
    if (++probe.settle_ticks < 6) return G_SOURCE_CONTINUE;
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
    if (probe.allocations != probe.stable_allocations)
      std::cerr << "GTK no-op allocations before=" << probe.stable_allocations
          << " after=" << probe.allocations << " viewport="
          << gtk_widget_get_allocated_width(presenter.ClientViewport()) << 'x'
          << gtk_widget_get_allocated_height(presenter.ClientViewport())
          << " environment=" << device->observation_revision << '\n';
    Check(probe.allocations == probe.stable_allocations, "unchanged presentation does not restart GTK allocation");
    CheckCustomPalette(presenter.TopLevelWindow());
    CheckRadio(presenter, 3);
    std::cerr << "PASS: four published native menu choices, three Add actions, child palette, locale, menu-aware content size and quiet allocation\n";
    gtk_menu_item_activate(GTK_MENU_ITEM(presenter.LayoutMenuItem(2)));
    Check(catalog.persisted.main_window->layout_state->EffectiveRoot().flow != ui::UiLayoutFlow::kVertical,
          "native layout input waits for published model Event");
    ++probe.stage;
  } else if (probe.stage == 11) {
    if (catalog.persisted.main_window->layout_state->EffectiveRoot().flow != ui::UiLayoutFlow::kVertical) return G_SOURCE_CONTINUE;
    Check(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(presenter.LayoutMenuItem(2))), "published layout check");
    gtk_menu_item_activate(GTK_MENU_ITEM(presenter.LayoutMenuItem(3)));
    ++probe.stage;
  } else if (probe.stage == 12) {
    if (catalog.persisted.main_window->layout_state->EffectiveRoot().flow == ui::UiLayoutFlow::kVertical) return G_SOURCE_CONTINUE;
    auto const* placement = device->PlacementFor(presenter.window.id());
    if (!placement) return G_SOURCE_CONTINUE;
    probe.normal_frame = placement->frame_dip;
    gtk_button_clicked(GTK_BUTTON(presenter.FullscreenAction()));
    Check(!static_cast<CatalogUiState&>(*device).IsWindowFullscreen(presenter.window.id()), "fullscreen click waits for model publication");
    ++probe.stage;
  } else if (probe.stage == 13) {
    auto& state = static_cast<CatalogUiState&>(*device);
    if (!state.IsWindowFullscreen(presenter.window.id()) || !NativeFullscreen(presenter)) return G_SOURCE_CONTINUE;
    Check(device->PlacementFor(presenter.window.id())->frame_dip == probe.normal_frame, "fullscreen preserves normal LOCAL geometry");
    Check(std::string{gtk_button_get_label(GTK_BUTTON(presenter.FullscreenAction()))}.find("Esc") != std::string::npos, "fullscreen offers native localized exit");
    Key(presenter, GDK_KEY_Escape);
    ++probe.stage;
  } else if (probe.stage == 14) {
    if (static_cast<CatalogUiState&>(*device).IsWindowFullscreen(presenter.window.id()) || NativeFullscreen(presenter)) return G_SOURCE_CONTINUE;
    GdkRectangle outer{}; gdk_window_get_frame_extents(gtk_widget_get_window(presenter.TopLevelWindow()), &outer);
    if (gtk_widget_get_allocated_width(presenter.ClientViewport()) != 600 || gtk_widget_get_allocated_height(presenter.ClientViewport()) != 320) return G_SOURCE_CONTINUE;
    bool exact_outer_supported = true;
#ifdef GDK_WINDOWING_WAYLAND
    exact_outer_supported = !GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(presenter.TopLevelWindow()));
#endif
    if (exact_outer_supported && (outer.width != probe.normal_frame.width || outer.height != probe.normal_frame.height)) return G_SOURCE_CONTINUE;
    Check(device->PlacementFor(presenter.window.id())->frame_dip == probe.normal_frame, "native exit restores content and retains saved outer preference");
    auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows.front());
    auto& child = static_cast<CatalogPresenter&>(*holder.window->presenter);
    probe.fullscreen_child = holder.window.id();
    Key(child, GDK_KEY_F11);
    ++probe.stage;
  } else if (probe.stage == 15) {
    auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows.front());
    auto& child = static_cast<CatalogPresenter&>(*holder.window->presenter);
    if (!static_cast<CatalogUiState&>(*device).IsWindowFullscreen(probe.fullscreen_child) || !NativeFullscreen(child)) return G_SOURCE_CONTINUE;
    Check(!NativeFullscreen(presenter), "child fullscreen is independent of main");
    gtk_window_close(GTK_WINDOW(child.TopLevelWindow()));
    ++probe.stage;
  } else if (probe.stage == 16) {
    auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows.front());
    if (catalog.IsHolderWindowOpen(holder)) return G_SOURCE_CONTINUE;
    Check(static_cast<CatalogUiState&>(*device).IsWindowFullscreen(probe.fullscreen_child), "Close retains child LOCAL fullscreen");
    presenter.model_proxy->Invoke(catalog.obj_id, &WindowCatalog::OpenHolderByIdFromUi, holder.obj_id);
    ++probe.stage;
  } else if (probe.stage == 17) {
    auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows.front());
    if (!catalog.IsHolderWindowOpen(holder)) return G_SOURCE_CONTINUE;
    auto& child = static_cast<CatalogPresenter&>(*holder.window->presenter);
    if (!gtk_widget_get_mapped(child.TopLevelWindow()) || !NativeFullscreen(child)) return G_SOURCE_CONTINUE;
    Check(holder.window.id() == probe.fullscreen_child, "reopen retains original child identity");
    presenter.model_proxy->Invoke(holder.window.id(), &ui::UiDesktopWindow::RequestFrame, 42, 52, 410, 280);
    probe.stage = 19;
  } else if (probe.stage == 19) {
    auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows.front());
    auto& child = static_cast<CatalogPresenter&>(*holder.window->presenter);
    if (holder.window->restore_client_width() != 410) return G_SOURCE_CONTINUE;
    Check(NativeFullscreen(child), "explicit normal frame request preserves native fullscreen");
    presenter.model_proxy->Invoke(holder.window.id(), &ui::UiDesktopWindow::RequestFrame, 42, 52, 410, 280);
    probe.settle_ticks = 0;
    probe.stage = 20;
  } else if (probe.stage == 20) {
    if (++probe.settle_ticks < 3) return G_SOURCE_CONTINUE;
    auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows.front());
    auto& child = static_cast<CatalogPresenter&>(*holder.window->presenter);
    Key(child, GDK_KEY_Escape);
    Key(presenter, GDK_KEY_F11);
    probe.stage = 18;
  } else if (probe.stage == 18) {
    if (!NativeFullscreen(presenter)) return G_SOURCE_CONTINUE;
    auto& state = static_cast<CatalogUiState&>(*device);
    if (!state.IsWindowFullscreen(presenter.window.id()) || state.IsWindowFullscreen(probe.fullscreen_child)) return G_SOURCE_CONTINUE;
    auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows.front());
    auto& child = static_cast<CatalogPresenter&>(*holder.window->presenter);
    if (gtk_widget_get_allocated_width(child.ClientViewport()) != 410 || gtk_widget_get_allocated_height(child.ClientViewport()) != 280) return G_SOURCE_CONTINUE;
    Check(!NativeFullscreen(child), "child exits fullscreen after pending equal-data RequestFrame");
    Check(holder.window->restore_client_width() == 410 && holder.window->restore_client_height() == 280,
          "latest explicit normal request applies after fullscreen exit");
    auto const actual_normal = device->PlacementFor(presenter.window.id())->frame_dip;
    if (actual_normal != probe.normal_frame)
      std::cerr << "normal before=" << probe.normal_frame.x << ',' << probe.normal_frame.y << ',' << probe.normal_frame.width << ',' << probe.normal_frame.height
          << " after=" << actual_normal.x << ',' << actual_normal.y << ',' << actual_normal.width << ',' << actual_normal.height << '\n';
    Check(actual_normal == probe.normal_frame, "fullscreen normal geometry survived independent child lifecycle");
    Check(catalog.persisted.main_window->restore_client_width() == 600 && catalog.persisted.main_window->restore_client_height() == 320,
          "fullscreen native pixels never rewrote USER semantic frame");
    std::cerr << "PASS: native layout/reset, main/child fullscreen, F11/Esc, Close/reopen and LOCAL-only normal geometry\n";
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
  for (int index = 2; index < argc; ++index) {
    std::string_view const argument{argv[index]};
    if (argument == "--restore") probe.restore = true;
    else if (argument == "--display-only") probe.display_only = true;
    else Check(false, "known GTK acceptance argument");
  }
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
