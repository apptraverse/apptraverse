// Linux GTK3 smoke test for AppTraverse Chat (same behavioral contract as Windows).
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include <gtk/gtk.h>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "aether_link.h"
#include "chat_commands.h"
#include "chat_model.h"
#include "linux_chat_app.h"

namespace apptraverse::example::chat_demo {
namespace {

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

inline constexpr ae::ObjId kWorkspaceRootId{10001};

void PumpGtk(std::chrono::milliseconds slice) {
  auto const deadline = std::chrono::steady_clock::now() + slice;
  while (std::chrono::steady_clock::now() < deadline) {
    while (gtk_events_pending() != 0) {
      gtk_main_iteration();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
}

void SaveWorkspaceGraph(ChatWorkspace::ptr const& ws) {
  ws.Save();
  for (auto& entry : ws->chats) {
    if (entry.is_valid()) {
      entry.Save();
      if (entry->peer_link.is_valid()) {
        entry->peer_link.Save();
      }
      if (entry->room.is_valid()) {
        entry->room.Save();
      }
    }
  }
}

AetherLink::ptr CreateAetherLink(ae::Domain& domain, ae::ObjId id, std::string endpoint) {
  auto link = AetherLink::ptr::Create(ae::CreateWith{domain}.with_id(id));
  link->endpoint_uid = std::move(endpoint);
  InitializeRuntimeNode(*link);
  return link;
}

ChatRoom::ptr CreateRoom(ae::Domain& domain, ae::ObjId id) {
  auto room = ChatRoom::ptr::Create(ae::CreateWith{domain}.with_id(id));
  InitializeRuntimeNode(*room);
  return room;
}

void SeedSmokeWorkspace(std::filesystem::path const& state_dir) {
  auto const model_dir = state_dir / "model";
  std::filesystem::remove_all(model_dir);
  std::filesystem::create_directories(model_dir);

  EnsureAetherLinkRegistration();
  apptraverse::DirectoryDomainStorage storage{model_dir};
  ae::Domain domain{storage};

  auto ws = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(kWorkspaceRootId));
  InitializeRuntimeNode(*ws);
  BindLocalEndpoint(*ws, "11111111-2222-3333-4444-555555555555");

  DesktopBounds bounds{
      .valid = true,
      .x = -120,
      .y = 80,
      .width = 920,
      .height = 640,
      .maximized = false,
  };
  SetDesktopBounds(*ws, bounds);

  auto e1 = OpenOrSelectChat(*ws, "peer-alpha", "Alice");
  auto e2 = OpenOrSelectChat(*ws, "peer-beta", "Bob");
  SelectChat(*ws, e1.id());

  auto link1 = CreateAetherLink(domain, ae::ObjId{2001}, "22222222-3333-4444-5555-666666666666");
  auto room1 = CreateRoom(domain, ae::ObjId{3001});
  BindChat(*e1, link1, room1);

  auto link2 = CreateAetherLink(domain, ae::ObjId{2002}, "33333333-4444-5555-6666-777777777777");
  auto room2 = CreateRoom(domain, ae::ObjId{3002});
  BindChat(*e2, link2, room2);

  for (int i = 0; i < 180; ++i) {
    std::string text = "Message " + std::to_string(i) +
                       "\nEmoji line 🎉\nWrapped multiline body with enough height to "
                       "scroll far past sixty-five thousand logical pixels when accumulated.";
    SetDraft(*e1, text);
    SubmitDraft(*ws, *e1, static_cast<std::uint64_t>(10000 + i));
  }

  SetDraft(*e1, "Alpha draft preserved");
  SetDraft(*e2, "Beta draft preserved");

  ScrollAnchor anchor_mid{
      .follow_tail = false,
      .first_visible_message = room1->messages[40].id,
      .offset_from_message_top = 18.0,
  };
  SetScroll(*e1, anchor_mid);

  SaveWorkspaceGraph(ws);
}

bool WaitForSnapshot(LinuxChatApp& app, LinuxChatGuiSnapshot& snap,
                     std::chrono::milliseconds timeout, int min_chats = 1) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (app.TryQueryGuiSnapshot(snap) && snap.workspace_ready && snap.chat_count >= min_chats) {
      return true;
    }
    PumpGtk(std::chrono::milliseconds{20});
  }
  return app.TryQueryGuiSnapshot(snap) && snap.workspace_ready && snap.chat_count >= min_chats;
}

void SelectChatByIndex(LinuxChatApp& app, int index) {
  app.TestSelectChatByIndex(index);
  PumpGtk(std::chrono::milliseconds{150});
}

void TestLinuxChatSmoke() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  EnsureChatDemoModelRegistration();
  EnsureAetherLinkRegistration();

  std::filesystem::path const test_dir =
      std::filesystem::temp_directory_path() / "apptraverse_linux_chat_smoke";
  std::filesystem::remove_all(test_dir);
  std::filesystem::create_directories(test_dir);

  SeedSmokeWorkspace(test_dir);

  std::cout << "Starting Linux GTK smoke test harness...\n";

  {
    ChatLaunchOptions options{.state_dir = test_dir.string()};

    LinuxChatApp app;
    std::thread gui{[&] { CHECK(app.Run(options) == 0); }};

    PumpGtk(std::chrono::milliseconds{200});
    CHECK(app.main_window() != nullptr);
    CHECK(app.chat_list() != nullptr);
    CHECK(app.transcript_view() != nullptr);
    CHECK(app.draft_view() != nullptr);

    LinuxChatGuiSnapshot snap{};
    CHECK(WaitForSnapshot(app, snap, std::chrono::seconds{15}, 2));
    CHECK(snap.chat_count >= 2);
    CHECK(snap.draft.find("Alpha draft preserved") != std::string::npos);
    CHECK(snap.bounds.valid);
    CHECK(snap.bounds.x == -120);
    CHECK(snap.bounds.width == 920);

    SelectChatByIndex(app, 1);
    CHECK(app.TryQueryGuiSnapshot(snap));
    CHECK(snap.list_selection == 1);
    CHECK(snap.draft.find("Beta draft preserved") != std::string::npos);

    SelectChatByIndex(app, 0);
    CHECK(app.TryQueryGuiSnapshot(snap));
    CHECK(snap.draft.find("Alpha draft preserved") != std::string::npos);

    std::string const typed = "Typing during incoming 日本語";
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(app.draft_view())),
                             typed.c_str(), static_cast<gint>(typed.size()));
    PumpGtk(std::chrono::milliseconds{80});
    CHECK(app.TryQueryGuiSnapshot(snap));
    CHECK(snap.draft == typed);

    CHECK(!snap.model_scroll.follow_tail);
    CHECK(snap.active_entry_id.is_valid());
    CHECK(std::abs(snap.model_scroll.offset_from_message_top - 18.0) < 0.1);

    GtkWidget* parent = gtk_widget_get_parent(app.transcript_view());
    while (parent != nullptr && !GTK_IS_SCROLLED_WINDOW(parent)) {
      parent = gtk_widget_get_parent(parent);
    }
    CHECK(parent != nullptr);
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));
    gdouble const upper = gtk_adjustment_get_upper(adj);
    gtk_adjustment_set_value(adj, upper * 0.25);
    PumpGtk(std::chrono::milliseconds{50});
    CHECK(app.TryQueryGuiSnapshot(snap));
    ScrollAnchor const captured = snap.measured_scroll;
    CHECK(!captured.follow_tail);
    app.session().SaveScroll(snap.active_entry_id, captured);
    PumpGtk(std::chrono::milliseconds{200});

    SelectChatByIndex(app, 1);
    SelectChatByIndex(app, 0);
    PumpGtk(std::chrono::milliseconds{150});
    CHECK(app.TryQueryGuiSnapshot(snap));
    CHECK(snap.measured_scroll.first_visible_message == captured.first_visible_message);
    CHECK(std::abs(snap.measured_scroll.offset_from_message_top -
                   captured.offset_from_message_top) < 12.0);

    gtk_window_maximize(GTK_WINDOW(app.main_window()));
    PumpGtk(std::chrono::milliseconds{100});
    gtk_widget_destroy(app.main_window());
    gui.join();
    std::cout << "  First run closed cleanly.\n";
  }

  {
    ChatLaunchOptions options{.state_dir = test_dir.string()};

    LinuxChatApp app;
    std::thread gui{[&] { CHECK(app.Run(options) == 0); }};

    PumpGtk(std::chrono::milliseconds{200});

    LinuxChatGuiSnapshot snap{};
    CHECK(WaitForSnapshot(app, snap, std::chrono::seconds{15}, 2));
    CHECK(snap.bounds.valid);
    CHECK(snap.bounds.x == -120);
    CHECK(!snap.model_scroll.follow_tail);

    gtk_widget_destroy(app.main_window());
    gui.join();
    std::cout << "  Second run reload and shutdown passed.\n";
  }

  {
    std::filesystem::path const cold_dir = test_dir / "cold";
    std::filesystem::create_directories(cold_dir);
    ChatLaunchOptions options{
        .state_dir = cold_dir.string(),
        .open_peer = OpenPeerRequest{.peer_admin_id = "connecting-peer"},
    };

    LinuxChatApp app;
    std::thread gui{[&] { CHECK(app.Run(options) == 0); }};
    PumpGtk(std::chrono::milliseconds{100});
    gtk_widget_destroy(app.main_window());
    gui.join();
    std::cout << "  Close while connecting passed.\n";
  }
}

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  apptraverse::example::chat_demo::TestLinuxChatSmoke();
  std::cout << "apptraverse_chat_linux_smoke_test passed!\n";
  return 0;
}
