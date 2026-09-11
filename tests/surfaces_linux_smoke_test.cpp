#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtk/gtk.h>

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "linux_app.h"
#include "linux_presenters.h"
#include "surfaces_ids.h"
#include "surfaces_lifecycle.h"
#include "surfaces_model.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

struct GuiSync {
  std::mutex mu;
  std::condition_variable cv;
  bool done{false};
  std::function<void()> work;
};

gboolean RunGuiSync(gpointer data) {
  auto* sync = static_cast<GuiSync*>(data);
  sync->work();
  {
    std::lock_guard<std::mutex> lock{sync->mu};
    sync->done = true;
  }
  sync->cv.notify_one();
  return G_SOURCE_REMOVE;
}

void OnGuiDirect(std::function<void()> work) {
  GuiSync sync;
  sync.work = std::move(work);
  sync.done = false;
  gdk_threads_add_idle(&RunGuiSync, &sync);
  std::unique_lock<std::mutex> lock{sync.mu};
  CHECK(sync.cv.wait_for(lock, std::chrono::seconds{30},
                         [&] { return sync.done; }));
}

std::vector<GtkWindow*> ListSurfaceWindows() {
  std::vector<GtkWindow*> out;
  GList* toplevels = gtk_window_list_toplevels();
  for (GList* it = toplevels; it != nullptr; it = it->next) {
    auto* window = GTK_WINDOW(it->data);
    char const* title = gtk_window_get_title(window);
    if (title != nullptr && std::string{title}.rfind("Surface ", 0) == 0 &&
        gtk_widget_get_visible(GTK_WIDGET(window))) {
      out.push_back(window);
    }
  }
  g_list_free(toplevels);
  return out;
}

GtkWindow* FindSurfaceWindow(char const* title) {
  for (GtkWindow* window : ListSurfaceWindows()) {
    char const* text = gtk_window_get_title(window);
    if (text != nullptr && std::string{text} == title) {
      return window;
    }
  }
  return nullptr;
}

GtkButton* FindButton(GtkWindow* window, char const* label) {
  GtkWidget* child = gtk_bin_get_child(GTK_BIN(window));
  if (!GTK_IS_CONTAINER(child)) {
    return nullptr;
  }
  GList* kids = gtk_container_get_children(GTK_CONTAINER(child));
  for (GList* it = kids; it != nullptr; it = it->next) {
    if (!GTK_IS_BUTTON(it->data)) {
      continue;
    }
    auto* button = GTK_BUTTON(it->data);
    char const* text = gtk_button_get_label(button);
    if (text != nullptr && std::string{text} == label) {
      g_list_free(kids);
      return button;
    }
  }
  g_list_free(kids);
  return nullptr;
}

bool WaitSurface(char const* title, GtkWindow** out,
                 std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    GtkWindow* found = nullptr;
    OnGuiDirect([&] { found = FindSurfaceWindow(title); });
    if (found != nullptr) {
      if (out != nullptr) {
        *out = found;
      }
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
}

bool WaitCount(int expected, std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    int count = -1;
    OnGuiDirect([&] { count = static_cast<int>(ListSurfaceWindows().size()); });
    if (count == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
}

void ClickButton(GtkWindow* window, char const* label) {
  OnGuiDirect([&] {
    GtkButton* button = FindButton(window, label);
    CHECK(button != nullptr);
    gtk_button_clicked(button);
  });
}

void ClickAdd(GtkWindow* window) { ClickButton(window, "Add"); }

void ClickClose(GtkWindow* window) {
  ClickButton(window, "Close this window");
}

void EmitDelete(GtkWindow* window) {
  OnGuiDirect([&] {
    gboolean handled = FALSE;
    g_signal_emit_by_name(window, "delete-event", nullptr, &handled);
  });
}

void ActivateWindow(LinuxApp& app, GtkWindow* window) {
  OnGuiDirect([&] {
    gtk_window_present(window);
    // Drive PageShown through the same presenter path focus-in uses. Automated
    // sessions often do not transfer real WM focus to the presented window.
    LinuxSurfacePresenter* presenter = app.PresenterFor(GTK_WIDGET(window));
    CHECK(presenter != nullptr);
    presenter->PageShown();
  });
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
}

struct Rect {
  int x;
  int y;
  int width;
  int height;
};

Rect ReadOuter(LinuxApp& app, GtkWindow* window) {
  Rect rect{};
  OnGuiDirect([&] {
    app.ReadOuterBounds(GTK_WIDGET(window), &rect.x, &rect.y, &rect.width,
                        &rect.height);
  });
  return rect;
}

void PlaceOuter(LinuxApp& app, GtkWindow* window, int x, int y, int w, int h) {
  OnGuiDirect(
      [&] { app.PlaceOuterWindow(GTK_WIDGET(window), x, y, w, h); });
}

bool RectNear(Rect const& a, Rect const& b, int tol) {
  return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol &&
         std::abs(a.width - b.width) <= tol &&
         std::abs(a.height - b.height) <= tol;
}

void TestPresenterHierarchy() {
  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();
  auto& registry = ae::Registry::GetRegistry();
  CHECK(registry.GenerationDistance(SurfacePresenter::kClassId,
                                    DesktopSurfacePresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(DesktopSurfacePresenter::kClassId,
                                    LinuxSurfacePresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(SurfacePresenter::kClassId,
                                    LinuxSurfacePresenter::kClassId) == 2);
}

void TestCloseButtonRemovesOne() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_gtk3_close_btn";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();

  LinuxApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  GtkWindow* s1 = nullptr;
  CHECK(WaitSurface("Surface 1", &s1, std::chrono::seconds{30}));
  ClickAdd(s1);
  GtkWindow* s2 = nullptr;
  CHECK(WaitSurface("Surface 2", &s2, std::chrono::seconds{30}));
  ClickAdd(s2);
  GtkWindow* s3 = nullptr;
  CHECK(WaitSurface("Surface 3", &s3, std::chrono::seconds{30}));
  CHECK(WaitCount(3, std::chrono::seconds{10}));

  ClickClose(s2);
  CHECK(WaitCount(2, std::chrono::seconds{30}));
  OnGuiDirect([&] {
    CHECK(FindSurfaceWindow("Surface 1") != nullptr);
    CHECK(FindSurfaceWindow("Surface 3") != nullptr);
    CHECK(FindSurfaceWindow("Surface 2") == nullptr);
    s1 = FindSurfaceWindow("Surface 1");
    s3 = FindSurfaceWindow("Surface 3");
  });
  CHECK(s1 != nullptr);
  CHECK(s3 != nullptr);
  ClickAdd(s3);
  GtkWindow* s4 = nullptr;
  CHECK(WaitSurface("Surface 4", &s4, std::chrono::seconds{30}));
  CHECK(WaitCount(3, std::chrono::seconds{10}));

  PlaceOuter(app, s1, 60, 70, 380, 250);
  PlaceOuter(app, s3, 160, 170, 400, 260);
  PlaceOuter(app, s4, 260, 270, 420, 270);
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  Rect const r1 = ReadOuter(app, s1);
  Rect const r3 = ReadOuter(app, s3);
  Rect const r4 = ReadOuter(app, s4);

  EmitDelete(s3);
  gui.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 3);
  CHECK(application->surfaces->surfaces[0]->number == 1);
  CHECK(application->surfaces->surfaces[1]->number == 3);
  CHECK(application->surfaces->surfaces[2]->number == 4);

  LinuxApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  GtkWindow* rs1 = nullptr;
  GtkWindow* rs3 = nullptr;
  GtkWindow* rs4 = nullptr;
  CHECK(WaitSurface("Surface 1", &rs1, std::chrono::seconds{30}));
  CHECK(WaitSurface("Surface 3", &rs3, std::chrono::seconds{30}));
  CHECK(WaitSurface("Surface 4", &rs4, std::chrono::seconds{30}));
  CHECK(WaitCount(3, std::chrono::seconds{10}));
  OnGuiDirect([&] { CHECK(FindSurfaceWindow("Surface 2") == nullptr); });
  constexpr int kTol = 40;
  CHECK(RectNear(ReadOuter(app2, rs1), r1, kTol));
  CHECK(RectNear(ReadOuter(app2, rs3), r3, kTol));
  CHECK(RectNear(ReadOuter(app2, rs4), r4, kTol));

  EmitDelete(rs1);
  gui2.join();
  std::filesystem::remove_all(dir);
}

void TestNativeCloseKeepsAllSurfaces() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_gtk3_native_x";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();

  LinuxApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  GtkWindow* s1 = nullptr;
  CHECK(WaitSurface("Surface 1", &s1, std::chrono::seconds{30}));
  ClickAdd(s1);
  GtkWindow* s2 = nullptr;
  CHECK(WaitSurface("Surface 2", &s2, std::chrono::seconds{30}));
  ClickAdd(s2);
  GtkWindow* s3 = nullptr;
  CHECK(WaitSurface("Surface 3", &s3, std::chrono::seconds{30}));
  CHECK(WaitCount(3, std::chrono::seconds{10}));

  PlaceOuter(app, s1, 50, 60, 370, 240);
  PlaceOuter(app, s2, 150, 160, 390, 250);
  PlaceOuter(app, s3, 250, 260, 410, 260);
  std::this_thread::sleep_for(std::chrono::milliseconds{100});
  Rect const r1 = ReadOuter(app, s1);
  Rect const r2 = ReadOuter(app, s2);
  Rect const r3 = ReadOuter(app, s3);

  EmitDelete(s2);
  gui.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 3);
  CHECK(application->surfaces->surfaces[0]->number == 1);
  CHECK(application->surfaces->surfaces[1]->number == 2);
  CHECK(application->surfaces->surfaces[2]->number == 3);

  LinuxApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  GtkWindow* rs1 = nullptr;
  GtkWindow* rs2 = nullptr;
  GtkWindow* rs3 = nullptr;
  CHECK(WaitSurface("Surface 1", &rs1, std::chrono::seconds{30}));
  CHECK(WaitSurface("Surface 2", &rs2, std::chrono::seconds{30}));
  CHECK(WaitSurface("Surface 3", &rs3, std::chrono::seconds{30}));
  constexpr int kTol = 40;
  CHECK(RectNear(ReadOuter(app2, rs1), r1, kTol));
  CHECK(RectNear(ReadOuter(app2, rs2), r2, kTol));
  CHECK(RectNear(ReadOuter(app2, rs3), r3, kTol));

  ClickClose(rs2);
  CHECK(WaitCount(2, std::chrono::seconds{30}));
  OnGuiDirect([&] {
    rs1 = FindSurfaceWindow("Surface 1");
    rs3 = FindSurfaceWindow("Surface 3");
  });
  ClickClose(rs1);
  CHECK(WaitCount(1, std::chrono::seconds{30}));
  GtkWindow* last = nullptr;
  OnGuiDirect([&] { last = FindSurfaceWindow("Surface 3"); });
  CHECK(last != nullptr);
  ClickClose(last);
  gui2.join();

  DirectoryDomainStorage storage2{dir};
  ae::Domain domain2{storage2};
  auto application2 = LoadApplication<Application>(
      domain2, ae::ObjId{surfaces_demo::ToObjId(
                   surfaces_demo::ObjId::Application)});
  CHECK(application2->surfaces->surfaces.size() == 1);
  CHECK(application2->surfaces->surfaces[0]->number == 3);

  std::filesystem::remove_all(dir);
}

void TestActiveZOrderRestored() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_gtk3_zorder";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();

  LinuxApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  GtkWindow* s1 = nullptr;
  CHECK(WaitSurface("Surface 1", &s1, std::chrono::seconds{30}));
  ClickAdd(s1);
  GtkWindow* s2 = nullptr;
  CHECK(WaitSurface("Surface 2", &s2, std::chrono::seconds{30}));
  ClickAdd(s2);
  GtkWindow* s3 = nullptr;
  CHECK(WaitSurface("Surface 3", &s3, std::chrono::seconds{30}));
  CHECK(WaitCount(3, std::chrono::seconds{10}));

  ActivateWindow(app, s2);
  std::this_thread::sleep_for(std::chrono::milliseconds{200});

  EmitDelete(s2);
  gui.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->mobile_current);
  CHECK(application->surfaces->mobile_current->number == 2);

  LinuxApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  GtkWindow* rs2 = nullptr;
  CHECK(WaitSurface("Surface 1", nullptr, std::chrono::seconds{30}));
  CHECK(WaitSurface("Surface 2", &rs2, std::chrono::seconds{30}));
  CHECK(WaitSurface("Surface 3", nullptr, std::chrono::seconds{30}));
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  bool is_active = false;
  OnGuiDirect([&] {
    is_active =
        gtk_window_is_active(rs2) || gtk_window_has_toplevel_focus(rs2);
  });
  CHECK(is_active);

  EmitDelete(rs2);
  gui2.join();
  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  // gtk_init runs inside LinuxApp::Run on the GUI thread.
  apptraverse::test::TestPresenterHierarchy();
  apptraverse::test::TestCloseButtonRemovesOne();
  apptraverse::test::TestNativeCloseKeepsAllSurfaces();
  apptraverse::test::TestActiveZOrderRestored();
  std::cout << "surfaces_linux_smoke_test OK\n";
  return 0;
}
