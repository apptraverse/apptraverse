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

GtkWidget* FindActionBox(GtkWindow* window) {
  GtkWidget* host = gtk_bin_get_child(GTK_BIN(window));
  if (!GTK_IS_CONTAINER(host)) {
    return nullptr;
  }
  GList* kids = gtk_container_get_children(GTK_CONTAINER(host));
  GtkWidget* box = nullptr;
  if (kids != nullptr) {
    box = GTK_WIDGET(kids->data);
  }
  g_list_free(kids);
  return box;
}

GtkButton* FindButton(GtkWindow* window, char const* label) {
  GtkWidget* box = FindActionBox(window);
  if (!GTK_IS_CONTAINER(box)) {
    return nullptr;
  }
  GList* kids = gtk_container_get_children(GTK_CONTAINER(box));
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

bool WaitOrientation(LinuxApp& app, GtkWindow* window, GtkOrientation expected,
                     std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    bool match = false;
    OnGuiDirect([&] {
      LinuxSurfacePresenter* presenter =
          app.PresenterFor(GTK_WIDGET(window));
      if (presenter == nullptr || presenter->action_box == nullptr) {
        return;
      }
      GtkOrientation const orient = gtk_orientable_get_orientation(
          GTK_ORIENTABLE(presenter->action_box));
      bool const wide = presenter->IsWide();
      bool const expect_wide = expected == GTK_ORIENTATION_HORIZONTAL;
      match = orient == expected && wide == expect_wide &&
              (expect_wide
                   ? presenter->surface->presentation_width >=
                         presenter->surface->presentation_height
                   : presenter->surface->presentation_width <
                         presenter->surface->presentation_height);
    });
    if (match) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
}

void ResizePresentation(LinuxApp& app, GtkWindow* window, int width,
                        int height) {
  OnGuiDirect([&] {
    // gtk_window_resize drives the content_host size-allocate path.
    gtk_window_resize(window, width, height);
  });
}

bool WaitPresentationSize(LinuxApp& app, GtkWindow* window, int width,
                          int height, std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    bool match = false;
    OnGuiDirect([&] {
      LinuxSurfacePresenter* presenter =
          app.PresenterFor(GTK_WIDGET(window));
      if (presenter == nullptr) {
        return;
      }
      match = presenter->surface->presentation_width == width &&
              presenter->surface->presentation_height == height;
    });
    if (match) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
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

// Every "Surface N" toplevel, hidden ones included: presenter unload must
// destroy its window, not merely hide it.
int CountSurfaceToplevels() {
  int count = 0;
  GList* toplevels = gtk_window_list_toplevels();
  for (GList* it = toplevels; it != nullptr; it = it->next) {
    char const* title = gtk_window_get_title(GTK_WINDOW(it->data));
    if (title != nullptr && std::string{title}.rfind("Surface ", 0) == 0) {
      ++count;
    }
  }
  g_list_free(toplevels);
  return count;
}

void HideWindow(GtkWindow* window) {
  OnGuiDirect([&] {
    gtk_widget_hide(GTK_WIDGET(window));
    CHECK(!gtk_widget_get_visible(GTK_WIDGET(window)));
  });
}

bool WaitMapped(GtkWindow* window, bool mapped,
                std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    bool match = false;
    OnGuiDirect(
        [&] { match = gtk_widget_get_mapped(GTK_WIDGET(window)) == mapped; });
    if (match) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
}

// The GUI mirror reached through a live presenter: the model state presenters
// actually render. Caller must already be on the GUI thread.
Surfaces& MirrorSurfaces(LinuxApp& app, GtkWindow* anchor) {
  LinuxSurfacePresenter* presenter = app.PresenterFor(GTK_WIDGET(anchor));
  CHECK(presenter != nullptr);
  return *presenter->surface->surfaces;
}

// Number of the mirror's current Surface, 0 when there is none.
std::uint32_t CurrentSurfaceNumber(LinuxApp& app, GtkWindow* anchor) {
  std::uint32_t number = 0;
  OnGuiDirect([&] {
    Surfaces const& surfaces = MirrorSurfaces(app, anchor);
    number = surfaces.mobile_current ? surfaces.mobile_current->number : 0;
  });
  return number;
}

bool WaitCurrentSurface(LinuxApp& app, GtkWindow* anchor,
                        std::uint32_t expected,
                        std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (CurrentSurfaceNumber(app, anchor) == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
}

bool WaitMirrorSurfaceCount(LinuxApp& app, GtkWindow* anchor, std::size_t n,
                            std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    std::size_t size = 0;
    OnGuiDirect([&] { size = MirrorSurfaces(app, anchor).surfaces.size(); });
    if (size == n) {
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
    // Drive PageShown through the same presenter path focus-in uses. Whether
    // the window manager also hands input focus to the presented window is its
    // own decision, so callers wait on the model instead.
    LinuxSurfacePresenter* presenter = app.PresenterFor(GTK_WIDGET(window));
    CHECK(presenter != nullptr);
    presenter->PageShown();
  });
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

// Which toplevel holds input focus is decided by the window manager: it may
// focus the window it mapped last and may move focus again afterwards, so
// gtk_window_is_active() is not a state AppTraverse can guarantee. What the
// application owns is the target of the Z-order restore — it must resolve
// Surfaces::mobile_current by identity (falling back to the last Surface) and
// present that Surface's own window, then report it through PageShown.
// gtk_window_present() showing a hidden window is plain GTK presentation, so
// that target is observable without asking the window manager for anything.
//
// For the same reason the shutdown snapshot (focused window → current Surface)
// is not asserted here: the focused window is the window manager's choice.
void TestActiveSurfaceRestoreTargetsPersistedSurface() {
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

  // Activation report: with the other Surfaces hidden, focus-in cannot name a
  // Surface this test did not select, so the model must settle on Surface 2
  // purely because of the presenter-path PageShown.
  HideWindow(s1);
  HideWindow(s3);
  ActivateWindow(app, s2);
  CHECK(WaitCurrentSurface(app, s2, 2, std::chrono::seconds{30}));

  EmitDelete(s2);
  gui.join();

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surfaces& surfaces = *application->surfaces;
    CHECK(surfaces.surfaces.size() == 3);
    // Persist a current Surface that is not the creation-order last one, so a
    // restore that ignored mobile_current would pick a different window.
    surfaces.surfaces[1]->MakeCurrent();
    application.Save();  // runtime-save-ok: test fixture
  }

  ae::ObjId surface2_id;
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto application = LoadApplication<Application>(
        domain, ae::ObjId{surfaces_demo::ToObjId(
                    surfaces_demo::ObjId::Application)});
    Surfaces& surfaces = *application->surfaces;
    CHECK(surfaces.mobile_current);
    CHECK(surfaces.mobile_current->number == 2);
    CHECK(&*surfaces.mobile_current == &*surfaces.surfaces[1]);
    CHECK(surfaces.surfaces.back()->number == 3);
    surface2_id = surfaces.mobile_current->obj_id;
  }

  LinuxApp app2;
  std::thread gui2{[&] { CHECK(app2.Run(dir) == 0); }};
  GtkWindow* rs1 = nullptr;
  GtkWindow* rs2 = nullptr;
  GtkWindow* rs3 = nullptr;
  CHECK(WaitSurface("Surface 1", &rs1, std::chrono::seconds{30}));
  CHECK(WaitSurface("Surface 2", &rs2, std::chrono::seconds{30}));
  CHECK(WaitSurface("Surface 3", &rs3, std::chrono::seconds{30}));
  CHECK(WaitCount(3, std::chrono::seconds{10}));

  // Every restored window is mapped and bound both ways to its own Surface.
  OnGuiDirect([&] {
    GtkWindow* const windows[] = {rs1, rs2, rs3};
    std::uint32_t const numbers[] = {1u, 2u, 3u};
    for (int i = 0; i < 3; ++i) {
      LinuxSurfacePresenter* presenter =
          app2.PresenterFor(GTK_WIDGET(windows[i]));
      CHECK(presenter != nullptr);
      CHECK(presenter->surface->number == numbers[i]);
      CHECK(&*LinuxSurfacePresenter::ptr{presenter->surface->presenter} ==
            presenter);
      CHECK(gtk_widget_get_mapped(GTK_WIDGET(windows[i])));
    }
    Surfaces const& mirror = MirrorSurfaces(app2, rs1);
    CHECK(mirror.surfaces.size() == 3);
    CHECK(mirror.surfaces[1]->obj_id == surface2_id);
  });

  // Hiding the other two leaves a single focusable toplevel, so once the
  // presenter path has reported Surface 2 the mirror's current Surface can no
  // longer move: the restore target below is fully determined.
  HideWindow(rs1);
  HideWindow(rs3);
  ActivateWindow(app2, rs2);
  CHECK(WaitCurrentSurface(app2, rs1, 2, std::chrono::seconds{30}));
  HideWindow(rs2);

  OnGuiDirect([&] {
    Surfaces const& mirror = MirrorSurfaces(app2, rs1);
    CHECK(mirror.mobile_current);
    CHECK(mirror.mobile_current->obj_id == surface2_id);
    app2.RestoreActiveSurfaceZOrder();
    // Exactly the current Surface is presented, not the last one.
    CHECK(gtk_widget_get_visible(GTK_WIDGET(rs2)));
    CHECK(!gtk_widget_get_visible(GTK_WIDGET(rs1)));
    CHECK(!gtk_widget_get_visible(GTK_WIDGET(rs3)));
  });
  CHECK(WaitMapped(rs2, true, std::chrono::seconds{30}));

  // Removing the current Surface clears mobile_current, and with every
  // remaining window hidden no focus-in can refill it. The restore must then
  // take the documented fallback: the last Surface, still resolved by identity.
  ClickClose(rs2);
  CHECK(WaitMirrorSurfaceCount(app2, rs1, 2, std::chrono::seconds{30}));
  OnGuiDirect([&] {
    Surfaces const& mirror = MirrorSurfaces(app2, rs1);
    CHECK(!mirror.mobile_current);
    CHECK(mirror.surfaces[0]->number == 1);
    CHECK(mirror.surfaces[1]->number == 3);
    // Presenter unload destroyed the removed window instead of hiding it.
    CHECK(CountSurfaceToplevels() == 2);

    app2.RestoreActiveSurfaceZOrder();
    CHECK(gtk_widget_get_visible(GTK_WIDGET(rs3)));
    CHECK(!gtk_widget_get_visible(GTK_WIDGET(rs1)));
  });
  CHECK(WaitMapped(rs3, true, std::chrono::seconds{30}));
  // The restore reports its target through PageShown, so the model follows it.
  CHECK(WaitCurrentSurface(app2, rs1, 3, std::chrono::seconds{30}));

  // Leave a single Surface so the shutdown snapshot has nothing to choose
  // between, then stop through the native close path.
  ClickClose(rs1);
  CHECK(WaitMirrorSurfaceCount(app2, rs3, 1, std::chrono::seconds{30}));
  OnGuiDirect([&] { CHECK(CountSurfaceToplevels() == 1); });

  EmitDelete(rs3);
  gui2.join();
  // No GUI loop left: unload destroyed every presenter window.
  CHECK(CountSurfaceToplevels() == 0);

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  Surfaces& surfaces = *application->surfaces;
  CHECK(surfaces.surfaces.size() == 1);
  CHECK(surfaces.surfaces[0]->number == 3);
  CHECK(surfaces.mobile_current);
  CHECK(&*surfaces.mobile_current == &*surfaces.surfaces[0]);

  std::filesystem::remove_all(dir);
}

void TestControlsFollowPresentationSize() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_gtk3_presentation_size";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureLinuxSurfacePresenterRegistration();

  LinuxApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  GtkWindow* s1 = nullptr;
  CHECK(WaitSurface("Surface 1", &s1, std::chrono::seconds{30}));
  OnGuiDirect([&] {
    CHECK(FindButton(s1, "Add") != nullptr);
    CHECK(FindButton(s1, "Close this window") != nullptr);
  });

  ResizePresentation(app, s1, 800, 400);
  CHECK(WaitOrientation(app, s1, GTK_ORIENTATION_HORIZONTAL,
                        std::chrono::seconds{30}));

  ResizePresentation(app, s1, 400, 800);
  CHECK(WaitOrientation(app, s1, GTK_ORIENTATION_VERTICAL,
                        std::chrono::seconds{30}));

  // Square is wide under presentation_width >= presentation_height.
  ResizePresentation(app, s1, 500, 500);
  CHECK(WaitOrientation(app, s1, GTK_ORIENTATION_HORIZONTAL,
                        std::chrono::seconds{30}));

  ResizePresentation(app, s1, 700, 300);
  CHECK(WaitOrientation(app, s1, GTK_ORIENTATION_HORIZONTAL,
                        std::chrono::seconds{30}));
  CHECK(WaitPresentationSize(app, s1, 700, 300, std::chrono::seconds{30}));

  ClickAdd(s1);
  GtkWindow* s2 = nullptr;
  CHECK(WaitSurface("Surface 2", &s2, std::chrono::seconds{30}));
  ResizePresentation(app, s2, 360, 640);
  CHECK(WaitOrientation(app, s2, GTK_ORIENTATION_VERTICAL,
                        std::chrono::seconds{30}));
  ClickClose(s2);
  CHECK(WaitCount(1, std::chrono::seconds{30}));
  OnGuiDirect([&] { CHECK(FindSurfaceWindow("Surface 1") != nullptr); });

  EmitDelete(s1);
  gui.join();

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  Surface& surface = *application->surfaces->surfaces[0];
  CHECK(surface.presentation_width == 700);
  CHECK(surface.presentation_height == 300);
  bool saw_size_event = false;
  for (auto const& entry : surface.journal) {
    if (entry.event->GetClassId() ==
        SurfacePresentationSizeChangedEvent::kClassId) {
      saw_size_event = true;
      break;
    }
  }
  CHECK(saw_size_event);

  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  // gtk_init runs inside LinuxApp::Run on the GUI thread.
  apptraverse::test::TestPresenterHierarchy();
  apptraverse::test::TestCloseButtonRemovesOne();
  apptraverse::test::TestNativeCloseKeepsAllSurfaces();
  apptraverse::test::TestActiveSurfaceRestoreTargetsPersistedSurface();
  apptraverse::test::TestControlsFollowPresentationSize();
  std::cout << "surfaces_linux_smoke_test OK\n";
  return 0;
}
