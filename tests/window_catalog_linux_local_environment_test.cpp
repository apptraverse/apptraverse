#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <gtk/gtk.h>
#include "window_catalog_graph.h"
#include "window_catalog_linux_application.h"
#include "window_catalog_model.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"
#include "apptraverse/platform/linux/linux_ui_list_presenter.h"

using namespace apptraverse;
namespace {
void Check(bool value, char const* why) {
  if (!value) { std::cerr << "CHECK failed: " << why << '\n'; std::abort(); }
}
struct Probe {
  WindowCatalog* catalog{};
  int stage{}, ticks{}, semantic_width{}, semantic_height{};
  bool transfer{}, outer_only{};
  int expected_outer_width{}, expected_outer_height{};
  bool verified_outer{};
  ae::ObjId reopened;
};
gboolean Tick(gpointer opaque) {
  auto& test = *static_cast<Probe*>(opaque);
  Check(++test.ticks < 400, "GTK local settings settled within 20 seconds");
  auto& catalog = *test.catalog;
  auto& presenter = static_cast<ui::linux::LinuxDesktopWindowPresenter&>(*catalog.persisted.main_window->presenter);
  auto* device = presenter.LocalDeviceState();
  if (test.transfer) {
    Check(device->has_environment, "transfer baseline observed before native presentation");
    if (!test.verified_outer && test.expected_outer_width > 0) {
      auto* native_window = gtk_widget_get_window(presenter.TopLevelWindow());
      GdkRectangle outer{}; gdk_window_get_frame_extents(native_window, &outer);
      if (outer.width != test.expected_outer_width || outer.height != test.expected_outer_height) return G_SOURCE_CONTINUE;
      auto observation = std::find_if(device->windows.begin(), device->windows.end(), [&](auto const& w) { return w.window_id == presenter.window.id(); });
      if (observation == device->windows.end() || observation->frame_dip.width != outer.width ||
          observation->frame_dip.height != outer.height || observation->monitor_key != device->environment.monitors.front().key)
        return G_SOURCE_CONTINUE;
      int client_width{}, client_height{};
      gtk_window_get_size(GTK_WINDOW(presenter.TopLevelWindow()), &client_width, &client_height);
      Check(outer.height > client_height, "decorated GTK distinguishes outer and client geometry");
      Check(client_width == test.expected_outer_width - (outer.width - gdk_window_get_width(native_window)) &&
          client_height == test.expected_outer_height - (outer.height - gdk_window_get_height(native_window)),
          "Windows outer DIPs converted to GTK client size at native boundary");
      auto const* placement = device->PlacementFor(presenter.window.id());
      Check(placement && placement->frame_dip.width == test.expected_outer_width && placement->frame_dip.height == test.expected_outer_height,
          "seeded desired outer dimensions survived native observations");
      std::cerr << "PASS: Windows-seeded outer=" << outer.width << 'x' << outer.height
          << " GTK-client=" << client_width << 'x' << client_height << " LOCAL-outer="
          << observation->frame_dip.width << 'x' << observation->frame_dip.height << '\n';
      test.verified_outer = true;
      if (test.outer_only) {
        std::cerr << "PASS: GTK cold outer-frame native roundtrip\n";
        gtk_window_close(GTK_WINDOW(presenter.TopLevelWindow()));
        return G_SOURCE_REMOVE;
      }
    }
    if (test.stage == 0) {
      Check(catalog.persisted.list->rows.size() == 2, "Windows two-holder catalog imported");
      Check(catalog.persisted.locale->language_index() == 0, "Windows locale imported");
      auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows[0]);
      Check(!catalog.IsHolderWindowOpen(holder), "Windows first child stayed closed");
      test.reopened = holder.window.id();
      auto& list = static_cast<ui::linux::LinuxListContainerPresenter&>(*catalog.persisted.presenters.list_presenter);
      auto* box = GTK_LIST_BOX(list.ListWidget());
      auto* row = gtk_list_box_get_row_at_index(box, 0);
      gtk_list_box_select_row(box, row);
      g_signal_emit_by_name(box, "row-activated", row);
      ++test.stage;
    } else {
      auto& holder = static_cast<WindowHolder&>(*catalog.persisted.list->rows[0]);
      if (!catalog.IsHolderWindowOpen(holder)) return G_SOURCE_CONTINUE;
      Check(holder.window.id() == test.reopened, "GTK reopened original Windows child identity");
      Check(device->environment.monitors.front().key != "old-disconnected-screen", "GTK observed destination displays");
      std::cerr << "PASS: native GTK Windows USER import, LOCAL baseline, original child identity reopen and shutdown\n";
      gtk_window_close(GTK_WINDOW(presenter.TopLevelWindow()));
      return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
  }
  if (test.stage == 0) {
    Check(device->has_environment, "OS baseline preceded initial publication");
    test.semantic_width = catalog.persisted.main_window->client_width();
    test.semantic_height = catalog.persisted.main_window->client_height();
    gtk_window_resize(GTK_WINDOW(presenter.TopLevelWindow()), 220, 190);
    g_object_set(gtk_settings_get_default(), "gtk-font-name", "Sans 18", "gtk-enable-animations", FALSE,
        "gtk-application-prefer-dark-theme", TRUE, nullptr);
    ++test.stage;
  } else if (test.stage == 1) {
    auto observation = std::find_if(device->windows.begin(), device->windows.end(), [&](auto const& w) { return w.window_id == presenter.window.id(); });
    GdkRectangle outer{}; gdk_window_get_frame_extents(gtk_widget_get_window(presenter.TopLevelWindow()), &outer);
    if (!device->environment.reduced_motion || device->environment.message_font.height != 18 ||
        observation == device->windows.end() || observation->frame_dip.width != outer.width) return G_SOURCE_CONTINUE;
    int client_width{}, client_height{};
    gtk_window_get_size(GTK_WINDOW(presenter.TopLevelWindow()), &client_width, &client_height);
    if (client_width != 220) return G_SOURCE_CONTINUE;
    Check(catalog.persisted.main_window->client_width() == test.semantic_width &&
        catalog.persisted.main_window->client_height() == test.semantic_height, "native pixels did not enter portable semantic frame");
    auto& button = static_cast<ui::linux::LinuxPushButtonPresenter&>(*catalog.persisted.presenters.add_button_presenter);
    auto* text = gtk_widget_create_pango_layout(button.ButtonWidget(), "Mg");
    int height{}; pango_layout_get_pixel_size(text, nullptr, &height); g_object_unref(text);
    Check(height >= 24, "native accessibility font was measured without shrinking");
    presenter.model_proxy->Invoke(device->obj_id, &ui::UiDeviceState::SetAppearance,
        ui::UiAppearancePreferences{ui::UiThemeMode::kDark, true});
    ++test.stage;
  } else if (test.stage == 2) {
    if (device->appearance != ui::UiAppearancePreferences{ui::UiThemeMode::kDark, true}) return G_SOURCE_CONTINUE;
    auto root = *presenter.LayoutRoot(); root.padding = 30;
    presenter.model_proxy->Invoke(catalog.persisted.main_window->layout_state.id(), &ui::UiLayoutState::SetOverride, root);
    ++test.stage;
  } else {
    if (presenter.LayoutRoot()->padding != 30) return G_SOURCE_CONTINUE;
    auto rect = presenter.ResolveChildLayout({0, 0, 1, 1, 5001});
    Check(rect.x == 30 && rect.y == 30, "published semantic constraint changed local geometry");
    Check(!device->environment.monitors.empty(), "GTK monitor environment observed");
    std::cerr << "PASS: GTK live font/motion/theme, native resize LOCAL only, soft-dark preference, constraint Event and overflow\n";
    gtk_window_close(GTK_WINDOW(presenter.TopLevelWindow()));
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}
}
int main(int argc, char** argv) {
  EnsureWindowCatalogGraphRegistration();
  auto profile = std::filesystem::temp_directory_path() / ("apptraverse-local-gtk-" + std::to_string(ae::ObjId::GenerateUnique().id()));
  if (argc >= 2) profile = argv[1];
  WindowCatalogLinuxApplication app{profile};
  Probe probe;
  probe.transfer = argc >= 2;
  if (argc >= 4) {
    probe.expected_outer_width = std::stoi(argv[2]); probe.expected_outer_height = std::stoi(argv[3]);
    probe.outer_only = argc == 5;
  }
  auto callbacks = app.MakeHostCallbacks();
  auto after_initial = std::move(callbacks.after_initial);
  callbacks.after_initial = [&](ae::Obj& root) {
    after_initial(root); probe.catalog = &static_cast<WindowCatalog&>(root);
    g_timeout_add(50, Tick, &probe);
  };
  Check(app.Run(std::move(callbacks)) == 0, "GTK local settings shutdown");
  Check(std::filesystem::file_size(profile / "user.bin") > 0 &&
      std::filesystem::file_size(profile / "local" / "user.bin") > 0, "both native partitions saved at shutdown");
  std::cerr << "LOCAL_PROFILE=" << profile << '\n';
}
