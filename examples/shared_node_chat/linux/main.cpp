#include <gio/gio.h>
#include <gtk/gtk.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/linux/linux_presentation_host.h"
#include "apptraverse/platform/linux/linux_ui_list_presenter.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"
#include "apptraverse/platform/linux/linux_ui_registration.h"
#include "apptraverse/platform/linux/linux_ui_text_control_presenter.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "chat_ui_session.h"

namespace gui = apptraverse::chat::gui;
namespace {
void NotifyNewMessage() {
  // The Freedesktop notification service is shared by GNOME, KDE and other
  // desktops. Resolve the session bus asynchronously so UI publication stays
  // responsive even when no notification daemon is running.
  g_bus_get(
      G_BUS_TYPE_SESSION, nullptr,
      [](GObject*, GAsyncResult* result, gpointer) {
        GError* error = nullptr;
        auto* bus = g_bus_get_finish(result, &error);
        if (!bus) {
          if (error) g_error_free(error);
          return;
        }
        g_dbus_connection_call(
            bus, "org.freedesktop.Notifications",
            "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
            "Notify",
            g_variant_new(
                "(susss@as@a{sv}i)", "SharedNode Chat", 0, "",
                "SharedNode Chat", "New message",
                g_variant_new_strv(nullptr, 0),
                g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0), -1),
            G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 3000, nullptr,
            nullptr, nullptr);
        g_object_unref(bus);
      },
      nullptr);
}
struct Application final {
  apptraverse::ui::linux::LinuxPresentationHost host{};
  std::unique_ptr<gui::Session> session;
  std::atomic<bool> closing{false}, queued{false};
  std::uint64_t copied{};
  std::vector<std::string> messages;
  bool initialized{};
  void Publish() {
    if (closing || queued.exchange(true)) return;
    g_idle_add_full(
        G_PRIORITY_DEFAULT,
        [](gpointer p) -> gboolean {
          auto& app = *static_cast<Application*>(p);
          app.queued = false;
          if (app.closing) return G_SOURCE_REMOVE;
          app.session->ApplyPublication(&app.host);
          auto* model = app.session->Mirror();
          if (!model) return G_SOURCE_REMOVE;
          if (model->copy_request != app.copied) {
            app.copied = model->copy_request;
            gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                                   model->connection.uid.c_str(), -1);
            app.session->Proxy().Invoke(gui::RootId(),
                                        &gui::ChatUi::ClipboardResult, true);
          }
          if (app.initialized && messages_changed(app, model))
            NotifyNewMessage();
          app.messages = model->connection.messages;
          app.initialized = true;
          return G_SOURCE_REMOVE;
        },
        this, nullptr);
  }
  static bool messages_changed(Application const& app,
                               gui::ChatUi const* model) {
    auto* window = gtk_window_list_toplevels();
    bool active = false;
    for (auto* i = window; i; i = i->next)
      active = active || gtk_window_is_active(GTK_WINDOW(i->data));
    g_list_free(window);
    if (active) return false;
    for (std::size_t i = app.messages.size();
         i < model->connection.messages.size(); ++i)
      if (model->connection.messages[i].find(model->editing.name + ": ") != 0)
        return true;
    return false;
  }
};
}  // namespace
int main(int argc, char** argv) {
  std::filesystem::path profile = "shared-chat-state";
  std::filesystem::path distill_output;
  std::string name = "Host", invite;
  bool host = true, load_only = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--profile" && i + 1 < argc)
      profile = argv[++i];
    else if (arg == "--name" && i + 1 < argc)
      name = argv[++i];
    else if (arg == "--host")
      host = true;
    else if (arg == "--load-only")
      load_only = true;
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
    else if (arg == "--distill-only" && i + 1 < argc)
      distill_output = argv[++i];
#endif
    else if (arg == "--client") {
      host = false;
      if (i + 1 < argc && std::string(argv[i + 1]).find("--") != 0)
        invite = argv[++i];
    } else
      return 2;
  }
  if (distill_output.empty() && !gtk_init_check(nullptr, nullptr)) return 1;
  using namespace apptraverse::ui::linux;
  EnsureLinuxUiPresenterRegistration();
  auto environment = distill_output.empty() ? ReadLinuxDeviceEnvironment()
                                            : apptraverse::ui::UiDeviceEnvironment{};
  gui::ConfigureUiFactories(
      {[](ae::Domain& d) {
         return LinuxDesktopWindowPresenter::ptr::Create(ae::CreateWith{d});
       },
       [](ae::Domain& d) {
         return LinuxPushButtonPresenter::ptr::Create(ae::CreateWith{d});
       },
       [](ae::Domain& d) {
         return LinuxTextControlPresenter::ptr::Create(ae::CreateWith{d});
       },
       [](ae::Domain& d) {
         return LinuxListContainerPresenter::ptr::Create(ae::CreateWith{d});
       },
       [environment](apptraverse::ui::UiDeviceState& state) {
         state.ObserveEnvironment(
             environment, apptraverse::ui::UiEnvironmentReason::kStartup);
       }});
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
  if (!distill_output.empty()) return gui::ExportUiSupply(distill_output);
#endif
  Application app;
  app.host.opaque = &app;
  app.host.on_desktop_window_user_close = [](void* p, ae::Obj*) {
    auto& app = *static_cast<Application*>(p);
    app.closing = true;
    gtk_main_quit();
  };
  app.session = std::make_unique<gui::Session>(
      profile, name, host, invite, [&] { app.Publish(); }, load_only);
  if (auto* push = std::getenv("APPTRAVERSE_PUSH_SERVICE_UID"))
    app.session->ConfigurePush(push);
  app.session->Start();
  gtk_main();
  app.closing = true;
  app.session->Stop();
  while (g_source_remove_by_user_data(&app)) {
  }
  return 0;
}
