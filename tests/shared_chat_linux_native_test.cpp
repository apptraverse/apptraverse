#include <gtk/gtk.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include "apptraverse/platform/linux/linux_presentation_host.h"
#include "apptraverse/platform/linux/linux_ui_list_presenter.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"
#include "apptraverse/platform/linux/linux_ui_registration.h"
#include "apptraverse/platform/linux/linux_ui_text_control_presenter.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "chat_controls.h"
#include "chat_ui_session.h"
using namespace apptraverse;
using namespace apptraverse::ui::linux;
namespace gui = apptraverse::chat::gui;
using namespace std::chrono_literals;
#define CHECK(x)                                            \
  do {                                                      \
    if (!(x)) {                                             \
      std::cerr << "FAIL " #x " line " << __LINE__ << '\n'; \
      std::abort();                                         \
    }                                                       \
  } while (false)
std::filesystem::path transport_profile;
struct Fixture {
  char const* stage = "startup";
  LinuxPresentationHost host;
  gui::Session session;
  explicit Fixture(std::filesystem::path path)
      : session(
            path, "LinuxNativeTest", true, "", [] {}, true, {},
            transport_profile) {}
  void Pump() {
    // A continuously rescheduled native frame must not hide test deadlines.
    for (int i = 0; i < 64 && gtk_events_pending(); ++i)
      gtk_main_iteration_do(FALSE);
    session.ApplyPublication(&host);
  }
  template <class F>
  void Until(F ready) {
    auto limit = std::chrono::steady_clock::now() + 90s;
    while (!ready()) {
      if (std::chrono::steady_clock::now() >= limit) {
        auto v = session.TransportView();
        std::cerr << "stage=" << stage << " ready=" << v.ready
                  << " room=" << v.in_room << " status=" << v.status
                  << " draft="
                  << (session.Mirror() ? session.Mirror()->editing.draft
                                       : std::string{})
                  << '\n';
        CHECK(false);
      }
      Pump();
      std::this_thread::sleep_for(2ms);
    }
    Pump();
  }
  void PumpFor(std::chrono::milliseconds duration) {
    auto const end = std::chrono::steady_clock::now() + duration;
    do {
      Pump();
      std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < end);
  }
  LinuxTextControlPresenter* Text(unsigned id) {
    for (auto const& p : session.Mirror()->native_presenters)
      if (p->GetClassId() == LinuxTextControlPresenter::kClassId) {
        auto* text = static_cast<LinuxTextControlPresenter*>(p.operator->());
        if (text->control_id == id) return text;
      }
    std::abort();
  }
  LinuxPushButtonPresenter* Button(unsigned id) {
    for (auto const& p : session.Mirror()->native_presenters)
      if (p->GetClassId() == LinuxPushButtonPresenter::kClassId) {
        auto* button = static_cast<LinuxPushButtonPresenter*>(p.operator->());
        if (button->control_id == id) return button;
      }
    std::abort();
  }
};
int main(int argc, char** argv) {
  CHECK((argc == 2 || argc == 3) && !std::filesystem::exists(argv[1]));
  if (argc == 3) transport_profile = argv[2];
  CHECK(gtk_init_check(nullptr, nullptr));
  EnsureLinuxUiPresenterRegistration();
  auto env = ReadLinuxDeviceEnvironment();
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
       [env](ui::UiDeviceState& s) {
         s.ObserveEnvironment(env, ui::UiEnvironmentReason::kStartup);
       }});
  {
    Fixture f{argv[1]};
    f.session.Start();
    f.Until([&] {
      auto* m = f.session.Mirror();
      return m && m->connection.in_room;
    });
    f.stage = "typing";
    auto* input = f.Text(chat::kInputControl)->NativeWidget();
    auto* uid = f.Text(chat::kUidControl)->NativeWidget();
    auto* history = f.Text(chat::kMessagesControl)->NativeWidget();
    CHECK(GTK_IS_ENTRY(input) && GTK_IS_TEXT_VIEW(history));
    CHECK(std::string{atk_object_get_name(gtk_widget_get_accessible(input))} ==
          "Message");
    CHECK(!gtk_editable_get_editable(GTK_EDITABLE(uid)) &&
          !gtk_text_view_get_editable(GTK_TEXT_VIEW(history)));
    CHECK(std::system("xdotool search --name '^SharedNode Chat - "
                      "LinuxNativeTest$' windowactivate --sync") == 0);
    gtk_widget_grab_focus(input);
    f.Pump();
    CHECK(std::system("xdotool type --clearmodifiers --delay 0 rapid-native") ==
          0);
    f.Until(
        [&] { return f.session.Mirror()->editing.draft == "rapid-native"; });
    CHECK(gtk_widget_has_focus(input));
    gtk_editable_set_position(GTK_EDITABLE(input), 4);
    auto device = f.session.Mirror()->device_state.id();
    f.session.Proxy().Invoke(
        device, &ui::UiDeviceState::SetAppearance,
        ui::UiAppearancePreferences{ui::UiThemeMode::kDark, true});
    f.Until([&] {
      return f.session.Mirror()->device_state->appearance.theme ==
             ui::UiThemeMode::kDark;
    });
    CHECK(gtk_widget_has_focus(input) &&
          gtk_editable_get_position(GTK_EDITABLE(input)) == 4);
    CHECK(std::string{gtk_entry_get_text(GTK_ENTRY(input))} == "rapid-native");
    f.stage = "remount";
    f.session.RemountPresentation();
    f.Pump();
    input = f.Text(chat::kInputControl)->NativeWidget();
    CHECK(std::string{gtk_entry_get_text(GTK_ENTRY(input))} == "rapid-native");
    CHECK(std::system("xdotool search --name '^SharedNode Chat - "
                      "LinuxNativeTest$' windowactivate --sync") == 0);
    gtk_widget_grab_focus(input);
    gtk_editable_set_position(GTK_EDITABLE(input), -1);
    f.Pump();
    CHECK(std::system("xdotool type --clearmodifiers --delay 0 -- -remount") ==
          0);
    f.Until([&] {
      return f.session.Mirror()->editing.draft == "rapid-native-remount";
    });
    f.stage = "enter";
    CHECK(std::system("xdotool key Return") == 0);
    f.Until(
        [&] { return f.session.Mirror()->connection.messages.size() == 1; });
    CHECK(f.session.Mirror()->editing.draft.empty());
    gtk_entry_set_text(GTK_ENTRY(input), "button-message Привет 🌍");
    f.Until([&] {
      return f.session.Mirror()->editing.draft == "button-message Привет 🌍";
    });
    gtk_button_clicked(
        GTK_BUTTON(f.Button(chat::kSendControl)->ButtonWidget()));
    f.Until(
        [&] { return f.session.Mirror()->connection.messages.size() == 2; });
    CHECK(gtk_widget_has_focus(input));

    f.stage = "resize and idle after message publication";
    auto* presenter = static_cast<LinuxDesktopWindowPresenter*>(
        f.session.Mirror()->presenter.operator->());
    auto* window = presenter->TopLevelWindow();
    int const previous_width = gtk_widget_get_allocated_width(input);
    int width{}, height{};
    gtk_window_get_size(GTK_WINDOW(window), &width, &height);
    gtk_window_resize(GTK_WINDOW(window), width + 100, height + 40);
    f.Until([&] { return gtk_widget_get_allocated_width(input) > previous_width; });
    f.PumpFor(800ms);
    unsigned allocations{};
    auto const handler = g_signal_connect(
        window, "size-allocate",
        G_CALLBACK(+[](GtkWidget*, GtkAllocation*, gpointer data) {
          ++*static_cast<unsigned*>(data);
        }),
        &allocations);
    f.PumpFor(1000ms);
    g_signal_handler_disconnect(window, handler);
    CHECK(allocations <= 3);

    f.session.Stop();
    CHECK(std::filesystem::exists(std::filesystem::path{argv[1]} /
                                  "local/user.bin"));
    CHECK(!std::filesystem::exists(std::filesystem::path{argv[1]} /
                                   "transport/user.bin"));
  }
  {
    Fixture f{argv[1]};
    f.stage = "cold";
    f.session.Start();
    f.Until([&] { return f.session.Mirror() != nullptr; });
    CHECK(f.session.Mirror()->editing.draft.empty() &&
          f.session.Mirror()->connection.messages.empty());
    CHECK(f.session.Mirror()->device_state->appearance.theme ==
          ui::UiThemeMode::kDark);
    f.session.Stop();
  }
  std::cout << "PASS GTK native input, window resize, stable idle layout, RAM "
               "restart and LOCAL cold restore\n";
}
