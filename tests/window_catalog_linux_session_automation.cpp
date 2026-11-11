#include "window_catalog_linux_session_automation.h"

#include <iostream>
#include <vector>

#include "window_catalog_linux_desktop_zorder.h"
#include "window_catalog_model.h"

#include "apptraverse/platform/linux/linux_desktop_window_stack.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/linux/linux_ui_list_presenter.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/ui/ui_presented_window.h"

#include <gtk/gtk.h>

namespace apptraverse::example::surfaces {
namespace {

void LogMarker(char const* marker) {
  std::cerr << marker << '\n';
}

WindowCatalog& AsCatalog(ae::Obj& ui_root) {
  return static_cast<WindowCatalog&>(ui_root);
}

ui::linux::LinuxListContainerPresenter& LinuxList(WindowCatalog& catalog) {
  catalog.persisted.presenters.list_presenter.Load();
  return static_cast<ui::linux::LinuxListContainerPresenter&>(
      *catalog.persisted.presenters.list_presenter.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxAdd(WindowCatalog& catalog) {
  catalog.persisted.presenters.add_button_presenter.Load();
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *catalog.persisted.presenters.add_button_presenter.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxOpen(WindowCatalog& catalog) {
  catalog.persisted.presenters.open_button_presenter.Load();
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *catalog.persisted.presenters.open_button_presenter.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxDelete(WindowCatalog& catalog) {
  catalog.persisted.presenters.delete_button_presenter.Load();
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *catalog.persisted.presenters.delete_button_presenter.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxLanguage(WindowCatalog& catalog) {
  catalog.persisted.presenters.language_button_presenter.Load();
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *catalog.persisted.presenters.language_button_presenter.operator->());
}

ui::linux::LinuxDesktopWindowPresenter& LinuxMainWindow(WindowCatalog& catalog) {
  catalog.persisted.main_window.Load();
  catalog.persisted.main_window->presenter.Load();
  return static_cast<ui::linux::LinuxDesktopWindowPresenter&>(
      *catalog.persisted.main_window->presenter.operator->());
}

ui::linux::LinuxDesktopWindowPresenter* LinuxChildForRow(WindowCatalog& catalog,
                                                         int row_index) {
  catalog.persisted.list.Load();
  if (!catalog.persisted.list ||
      row_index < 0 ||
      static_cast<std::size_t>(row_index) >= catalog.persisted.list->rows.size()) {
    return nullptr;
  }
  auto& holder = static_cast<WindowHolder&>(
      *catalog.persisted.list->rows[static_cast<std::size_t>(row_index)]);
  if (!holder.window.is_loaded()) {
    return nullptr;
  }
  auto& presented = static_cast<ui::UiPresentedWindow&>(*holder.window);
  presented.presenter.Load();
  if (!presented.presenter) {
    return nullptr;
  }
  return &static_cast<ui::linux::LinuxDesktopWindowPresenter&>(
      *presented.presenter.operator->());
}

void SelectListRowNative(ui::linux::LinuxListContainerPresenter& list,
                         int index) {
  GtkWidget* widget = list.ListWidget();
  GtkListBoxRow* row =
      gtk_list_box_get_row_at_index(GTK_LIST_BOX(widget), index);
  gtk_list_box_select_row(GTK_LIST_BOX(widget), row);
}

void ActivateListRowNative(ui::linux::LinuxListContainerPresenter& list,
                           int index) {
  GtkListBox* const box = GTK_LIST_BOX(list.ListWidget());
  GtkListBoxRow* const row = gtk_list_box_get_row_at_index(box, index);
  g_signal_emit_by_name(box, "row-activated", row);
}

void ClickButtonNative(ui::linux::LinuxPushButtonPresenter& button) {
  gtk_button_clicked(GTK_BUTTON(button.ButtonWidget()));
}

void CloseWindowNative(ui::linux::LinuxDesktopWindowPresenter& window) {
  gtk_window_close(GTK_WINDOW(window.TopLevelWindow()));
}

std::size_t RowCount(WindowCatalog& catalog) {
  catalog.persisted.list.Load();
  return catalog.persisted.list ? catalog.persisted.list->rows.size() : 0;
}

bool LocaleIs(WindowCatalog& catalog, int language_index) {
  catalog.persisted.locale.Load();
  return catalog.persisted.locale && catalog.persisted.locale->language_index() == language_index;
}

bool AddLabelIs(WindowCatalog& catalog, char const* expected) {
  catalog.persisted.texts.label_add.Load();
  return catalog.persisted.texts.label_add &&
         catalog.persisted.texts.label_add->Resolve(catalog.persisted.locale->language_index()) ==
             expected;
}

enum class AutomationStep {
  Idle,
  SaveAwaitAdd1,
  SaveAwaitAdd2,
  SaveAwaitAdd3,
  SaveAwaitSelectOpen,
  SaveAwaitFirstOpen,
  SaveAwaitReopenSame,
  SaveAwaitCloseChild,
  SaveAwaitReopenAfterClose,
  SaveAwaitLanguageRu,
  SaveAwaitLanguageEn,
  SaveAwaitSelectDelete,
  SaveAwaitDelete,
  SaveAwaitTwoRows,
  ColdAwaitRowCount,
  ColdAwaitLocale,
  ColdAwaitAdd,
  ColdAwaitThreeRows,
  Cycle2SaveAwaitAdd,
  GeomZAdd1,
  GeomZAdd2,
  GeomZAdd3,
  GeomZOpenRow0,
  GeomZOpenRow1,
  GeomZOpenRow1Select,
  GeomZOpenRow2,
  GeomZOpenRow2Select,
  GeomZAwaitGeomModel,
  GeomZAwaitZModel,
  GeomZColdVerify,
  GeomZCycle2ColdVerify,
  CcAwaitRows,
  CcAwaitSelect,
  CcAwaitOpen,
  CcAwaitIdentityFrames,
  CcAwaitClosed,
  CcAwaitReselect,
  CcAwaitReopened,
  CcAwaitFinalFrames,
  CcAwaitLanguage,
};

struct FrameExpect {
  int x;
  int y;
  int w;
  int h;
};

constexpr FrameExpect kGeomZMain1{100, 100, 640, 480};
constexpr FrameExpect kGeomZChild0_1{130, 150, 500, 360};
constexpr FrameExpect kGeomZChild1_1{200, 180, 450, 330};
constexpr FrameExpect kGeomZChild2_1{260, 210, 400, 290};

constexpr FrameExpect kGeomZMain2{140, 120, 620, 460};
constexpr FrameExpect kGeomZChild0_2{150, 160, 480, 340};
constexpr FrameExpect kGeomZChild1_2{220, 190, 430, 310};
constexpr FrameExpect kGeomZChild2_2{280, 220, 380, 270};

struct AutomationState {
  LinuxWindowCatalogSessionAutomation mode =
      LinuxWindowCatalogSessionAutomation::None;
  AutomationStep step = AutomationStep::Idle;
  ApplicationModelRuntime* runtime = nullptr;
  GtkWidget* first_open_widget = nullptr;
  FrameExpect cycle1_main{};
  FrameExpect cycle1_child[3]{};
  FrameExpect cycle2_main{};
  FrameExpect cycle2_child[3]{};
  bool cycle1_snapshotted = false;
  bool cycle2_snapshotted = false;
  int cc_row = 0;
  int cc_iteration = 0;
  int cc_selection_signals = 0;
  gulong cc_selection_handler = 0;
  std::int32_t cc_selection_index = -1;
};

AutomationState& State() {
  static AutomationState state;
  return state;
}

void CloseMain(WindowCatalog& catalog) {
  CloseWindowNative(LinuxMainWindow(catalog));
}

void MoveResizeNative(GtkWidget* widget, FrameExpect frame) {
  gtk_window_resize(GTK_WINDOW(widget), frame.w, frame.h);
  gtk_window_move(GTK_WINDOW(widget), frame.x, frame.y);
}

void RaiseNative(GtkWidget* widget) {
  GdkWindow* const gdk_window = gtk_widget_get_window(widget);
  if (gdk_window != nullptr) {
    gdk_window_raise(gdk_window);
  }
}

bool ReadNativeFrame(ui::linux::LinuxDesktopWindowPresenter& presenter,
                     FrameExpect& out) {
  GtkWidget* const widget = presenter.TopLevelWindow();
  if (widget == nullptr || !gtk_widget_get_realized(widget)) {
    return false;
  }
  gtk_window_get_position(GTK_WINDOW(widget), &out.x, &out.y);
  GtkWidget* const mount = presenter.ClientMount();
  if (mount != nullptr && gtk_widget_get_realized(mount)) {
    GtkAllocation allocation{};
    gtk_widget_get_allocation(mount, &allocation);
    out.w = allocation.width;
    out.h = allocation.height;
  } else {
    gtk_window_get_size(GTK_WINDOW(widget), &out.w, &out.h);
  }
  return true;
}

bool FrameMatchesExpect(FrameExpect actual, FrameExpect expected) {
  return actual.x == expected.x && actual.y == expected.y &&
         actual.w == expected.w && actual.h == expected.h;
}

bool FrameNearExpect(FrameExpect actual, FrameExpect expected) {
  auto near = [](int a, int b) { return a >= b - 8 && a <= b + 8; };
  return near(actual.x, expected.x) && near(actual.y, expected.y) &&
         near(actual.w, expected.w) && near(actual.h, expected.h);
}

bool CatalogWindowMirrorFrame(CatalogWindow& window, FrameExpect expected) {
  return window.x() == expected.x && window.y() == expected.y &&
         window.client_width() == expected.w &&
         window.client_height() == expected.h;
}

bool CatalogWindowNativeMatchesMirror(CatalogWindow& window) {
  auto& presented = static_cast<ui::UiPresentedWindow&>(window);
  presented.presenter.Load();
  if (!presented.presenter) {
    return false;
  }
  auto& presenter = static_cast<ui::linux::LinuxDesktopWindowPresenter&>(
      *presented.presenter);
  FrameExpect native{};
  if (!ReadNativeFrame(presenter, native)) {
    return false;
  }
  FrameExpect mirror{window.x(), window.y(), window.client_width(),
                     window.client_height()};
  return FrameMatchesExpect(native, mirror);
}

void SnapshotMirrorFrame(CatalogWindow& window, FrameExpect& out) {
  out.x = window.x();
  out.y = window.y();
  out.w = window.client_width();
  out.h = window.client_height();
}

bool CatalogMirrorNearCycle1Targets(WindowCatalog& catalog) {
  catalog.persisted.list.Load();
  catalog.persisted.main_window.Load();
  if (!FrameNearExpect(
          {catalog.persisted.main_window->x(), catalog.persisted.main_window->y(),
           catalog.persisted.main_window->client_width(),
           catalog.persisted.main_window->client_height()},
          kGeomZMain1)) {
    return false;
  }
  FrameExpect const child_frames[] = {kGeomZChild0_1, kGeomZChild1_1,
                                      kGeomZChild2_1};
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.persisted.list->rows[static_cast<std::size_t>(i)]);
    holder.window.Load();
    if (!FrameNearExpect({holder.window->x(), holder.window->y(),
                          holder.window->client_width(),
                          holder.window->client_height()},
                         child_frames[i])) {
      return false;
    }
  }
  return true;
}

bool CatalogAllOpenWindowsNativeMirrorSynced(WindowCatalog& catalog) {
  catalog.persisted.list.Load();
  catalog.persisted.main_window.Load();
  if (!CatalogWindowNativeMatchesMirror(*catalog.persisted.main_window)) {
    return false;
  }
  if (!catalog.persisted.list) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (LinuxChildForRow(catalog, i) == nullptr) {
      return false;
    }
    auto& holder = static_cast<WindowHolder&>(
        *catalog.persisted.list->rows[static_cast<std::size_t>(i)]);
    holder.window.Load();
    if (!CatalogWindowNativeMatchesMirror(*holder.window)) {
      return false;
    }
  }
  return true;
}

bool CatalogGeomZCycle1Matches(WindowCatalog& catalog) {
  if (!CatalogAllOpenWindowsNativeMirrorSynced(catalog)) {
    return false;
  }
  catalog.persisted.main_window.Load();
  if (!CatalogWindowMirrorFrame(*catalog.persisted.main_window, State().cycle1_main)) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.persisted.list->rows[static_cast<std::size_t>(i)]);
    holder.window.Load();
    if (!CatalogWindowMirrorFrame(*holder.window, State().cycle1_child[i])) {
      return false;
    }
  }
  return true;
}

bool CatalogGeomZCycle2Matches(WindowCatalog& catalog) {
  if (!CatalogAllOpenWindowsNativeMirrorSynced(catalog)) {
    return false;
  }
  catalog.persisted.main_window.Load();
  if (!CatalogWindowMirrorFrame(*catalog.persisted.main_window, State().cycle2_main)) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.persisted.list->rows[static_cast<std::size_t>(i)]);
    holder.window.Load();
    if (!CatalogWindowMirrorFrame(*holder.window, State().cycle2_child[i])) {
      return false;
    }
  }
  return true;
}

bool CatalogDesktopZOrderMatchesNative(WindowCatalog& catalog) {
  if (!ui::linux::LinuxDesktopStackUsesX11()) {
    WriteFatalStderr("fatal: geom/z smoke requires X11 GDK backend\n");
    std::abort();
  }
  std::vector<std::uint32_t> const native =
      CaptureCatalogDesktopZOrderTopToBottom(catalog);
  if (native.empty()) {
    return false;
  }
  bool matches = catalog.persisted.window_order.size() == native.size();
  for (std::size_t i = 0; matches && i < native.size(); ++i) {
    matches = catalog.persisted.window_order[i].is_valid() &&
        catalog.persisted.window_order[i].id().id() == native[i];
  }
  if (!matches) {
    std::cerr << "WINDOW_CATALOG_LINUX_ORDER model";
    for (auto const& window : catalog.persisted.window_order)
      std::cerr << ' ' << window.id().id();
    std::cerr << " native";
    for (auto id : native) std::cerr << ' ' << id;
    std::cerr << '\n';
  }
  return matches;
}

void ApplyGeomZCycle1Native(WindowCatalog& catalog) {
  MoveResizeNative(LinuxMainWindow(catalog).TopLevelWindow(), kGeomZMain1);
  MoveResizeNative(LinuxChildForRow(catalog, 0)->TopLevelWindow(),
                   kGeomZChild0_1);
  MoveResizeNative(LinuxChildForRow(catalog, 1)->TopLevelWindow(),
                   kGeomZChild1_1);
  MoveResizeNative(LinuxChildForRow(catalog, 2)->TopLevelWindow(),
                   kGeomZChild2_1);
  RaiseNative(LinuxChildForRow(catalog, 1)->TopLevelWindow());
  RaiseNative(LinuxMainWindow(catalog).TopLevelWindow());
  RaiseNative(LinuxChildForRow(catalog, 2)->TopLevelWindow());
  RaiseNative(LinuxChildForRow(catalog, 0)->TopLevelWindow());
}

void ApplyGeomZCycle2Native(WindowCatalog& catalog) {
  MoveResizeNative(LinuxMainWindow(catalog).TopLevelWindow(), kGeomZMain2);
  MoveResizeNative(LinuxChildForRow(catalog, 0)->TopLevelWindow(),
                   kGeomZChild0_2);
  MoveResizeNative(LinuxChildForRow(catalog, 1)->TopLevelWindow(),
                   kGeomZChild1_2);
  MoveResizeNative(LinuxChildForRow(catalog, 2)->TopLevelWindow(),
                   kGeomZChild2_2);
  RaiseNative(LinuxMainWindow(catalog).TopLevelWindow());
  RaiseNative(LinuxChildForRow(catalog, 2)->TopLevelWindow());
  RaiseNative(LinuxChildForRow(catalog, 1)->TopLevelWindow());
  RaiseNative(LinuxChildForRow(catalog, 0)->TopLevelWindow());
}

void RequireThreeCatalogRows(WindowCatalog& catalog) {
  if (RowCount(catalog) != 3) {
    WriteFatalStderr("fatal: geom/z expected three catalog rows\n");
    std::abort();
  }
}

void SnapshotCycle1ExpectationsFromMirror(WindowCatalog& catalog) {
  RequireThreeCatalogRows(catalog);
  catalog.persisted.main_window.Load();
  SnapshotMirrorFrame(*catalog.persisted.main_window, State().cycle1_main);
  catalog.persisted.list.Load();
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.persisted.list->rows[static_cast<std::size_t>(i)]);
    holder.window.Load();
    SnapshotMirrorFrame(*holder.window, State().cycle1_child[i]);
  }
  State().cycle1_snapshotted = true;
}

void SnapshotCycle2ExpectationsFromMirror(WindowCatalog& catalog) {
  RequireThreeCatalogRows(catalog);
  catalog.persisted.main_window.Load();
  SnapshotMirrorFrame(*catalog.persisted.main_window, State().cycle2_main);
  catalog.persisted.list.Load();
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.persisted.list->rows[static_cast<std::size_t>(i)]);
    holder.window.Load();
    SnapshotMirrorFrame(*holder.window, State().cycle2_child[i]);
  }
  State().cycle2_snapshotted = true;
}

void WhenCatalogWindowsSettled(WindowCatalog& catalog,
                               void (*then)(WindowCatalog&));
void LogCatalogNativeFrames(WindowCatalog& catalog, char const* phase);

void FinishGeomZCycle1Save(WindowCatalog& catalog) {
  FlushCatalogNativeFrames(catalog);
  SnapshotCycle1ExpectationsFromMirror(catalog);
  LogCatalogNativeFrames(catalog, "geom_saved");
  LogMarker("WINDOW_CATALOG_LINUX_GEOM_Z_MODEL_OK");
  CloseMain(catalog);
}

void ApplyGeomZCycle1WhenSettled(WindowCatalog& catalog) {
  ApplyGeomZCycle1Native(catalog);
  WhenCatalogWindowsSettled(catalog, &FinishGeomZCycle1Save);
}

void FinishGeomZCycle2Save(WindowCatalog& catalog) {
  FlushCatalogNativeFrames(catalog);
  SnapshotCycle2ExpectationsFromMirror(catalog);
  LogCatalogNativeFrames(catalog, "geom_saved");
  LogMarker("WINDOW_CATALOG_LINUX_GEOM_Z_CYCLE2_SAVE_OK");
  CloseMain(catalog);
}

void ApplyGeomZCycle2WhenSettled(WindowCatalog& catalog) {
  ApplyGeomZCycle2Native(catalog);
  WhenCatalogWindowsSettled(catalog, &FinishGeomZCycle2Save);
}

void HandleGeomZIncremental(WindowCatalog& catalog) {
  switch (State().step) {
    case AutomationStep::GeomZAdd1:
      if (RowCount(catalog) != 1) {
        return;
      }
      ClickButtonNative(LinuxAdd(catalog));
      State().step = AutomationStep::GeomZAdd2;
      return;
    case AutomationStep::GeomZAdd2:
      if (RowCount(catalog) != 2) {
        return;
      }
      ClickButtonNative(LinuxAdd(catalog));
      State().step = AutomationStep::GeomZAdd3;
      return;
    case AutomationStep::GeomZAdd3:
      if (RowCount(catalog) != 3) {
        return;
      }
      SelectListRowNative(LinuxList(catalog), 0);
      State().step = AutomationStep::GeomZOpenRow0;
      return;
    case AutomationStep::GeomZOpenRow0:
      catalog.persisted.list.Load();
      if (!catalog.persisted.list || catalog.persisted.list->selection_index != 0) {
        return;
      }
      ClickButtonNative(LinuxOpen(catalog));
      State().step = AutomationStep::GeomZOpenRow1;
      return;
    case AutomationStep::GeomZOpenRow1:
      if (LinuxChildForRow(catalog, 0) == nullptr) {
        return;
      }
      SelectListRowNative(LinuxList(catalog), 1);
      State().step = AutomationStep::GeomZOpenRow1Select;
      return;
    case AutomationStep::GeomZOpenRow1Select:
      catalog.persisted.list.Load();
      if (!catalog.persisted.list || catalog.persisted.list->selection_index != 1) {
        return;
      }
      ClickButtonNative(LinuxOpen(catalog));
      State().step = AutomationStep::GeomZOpenRow2;
      return;
    case AutomationStep::GeomZOpenRow2:
      if (LinuxChildForRow(catalog, 1) == nullptr) {
        return;
      }
      SelectListRowNative(LinuxList(catalog), 2);
      State().step = AutomationStep::GeomZOpenRow2Select;
      return;
    case AutomationStep::GeomZOpenRow2Select:
      catalog.persisted.list.Load();
      if (!catalog.persisted.list || catalog.persisted.list->selection_index != 2) {
        return;
      }
      ClickButtonNative(LinuxOpen(catalog));
      State().step = AutomationStep::GeomZAwaitGeomModel;
      return;
    case AutomationStep::GeomZAwaitGeomModel:
      if (LinuxChildForRow(catalog, 2) == nullptr) {
        return;
      }
      State().step = AutomationStep::Idle;
      WhenCatalogWindowsSettled(catalog, &ApplyGeomZCycle1WhenSettled);
      return;
    default:
      return;
  }
}

constexpr int kCcIterations = 6;
constexpr char const* kCcRowMarker = "apptraverse_cc_identity_row";

constexpr FrameExpect kCcIdentityMain{90, 70, 660, 500};
constexpr FrameExpect kCcIdentityChild[3] = {
    {300, 140, 420, 300}, {360, 200, 440, 320}, {420, 260, 460, 340}};
constexpr FrameExpect kCcCloseFrames[kCcIterations] = {
    {500, 120, 380, 260}, {520, 160, 400, 280}, {540, 200, 420, 300},
    {320, 300, 360, 250}, {340, 320, 380, 270}, {360, 340, 400, 290}};
constexpr FrameExpect kCcFinalMain{0, 0, 600, 450};
constexpr FrameExpect kCcFinalChild[3] = {
    {610, 40, 350, 240}, {640, 300, 370, 260}, {200, 470, 390, 280}};

void FailCc(char const* message) {
  WriteFatalStderr(message);
  std::abort();
}

bool MirrorNear(CatalogWindow& window, FrameExpect expected) {
  return FrameNearExpect({window.x(), window.y(), window.client_width(),
                          window.client_height()},
                         expected);
}

// Raw client-area origin in root coordinates and client size, independent of
// the presenter's report conversion; compared across processes by the smoke.
bool ReadRawClientFrame(ui::linux::LinuxDesktopWindowPresenter& presenter,
                        FrameExpect& out) {
  GtkWidget* const widget = presenter.TopLevelWindow();
  if (widget == nullptr || !gtk_widget_get_mapped(widget)) {
    return false;
  }
  gdk_window_get_origin(gtk_widget_get_window(widget), &out.x, &out.y);
  GtkAllocation allocation{};
  gtk_widget_get_allocation(presenter.ClientMount(), &allocation);
  out.w = allocation.width;
  out.h = allocation.height;
  return true;
}

CatalogWindow& RowWindow(WindowCatalog& catalog, int row_index) {
  catalog.persisted.list.Load();
  auto& holder = static_cast<WindowHolder&>(
      *catalog.persisted.list->rows[static_cast<std::size_t>(row_index)]);
  holder.window.Load();
  return *holder.window;
}

bool CatalogFramesNear(WindowCatalog& catalog, FrameExpect main,
                       FrameExpect const* children) {
  catalog.persisted.main_window.Load();
  if (!MirrorNear(*catalog.persisted.main_window, main)) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (LinuxChildForRow(catalog, i) == nullptr ||
        !MirrorNear(RowWindow(catalog, i), children[i])) {
      return false;
    }
  }
  return true;
}

bool ReadCatalogRawFrames(WindowCatalog& catalog, FrameExpect out[4]) {
  if (!ReadRawClientFrame(LinuxMainWindow(catalog), out[0])) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    ui::linux::LinuxDesktopWindowPresenter* child = LinuxChildForRow(catalog, i);
    if (child == nullptr || !ReadRawClientFrame(*child, out[i + 1])) {
      return false;
    }
  }
  return true;
}

void LogCatalogRawFrames(FrameExpect const frames[4]) {
  char const* const names[4] = {"main", "child0", "child1", "child2"};
  for (int i = 0; i < 4; ++i) {
    std::cerr << "WINDOW_CATALOG_LINUX_RAW_FRAME " << names[i] << ' '
              << frames[i].x << ' ' << frames[i].y << ' ' << frames[i].w << ' '
              << frames[i].h << '\n';
  }
}

// The model stores frame origin and client size, not raw client origin.
// Keep the raw snapshots above as an independent cross-process observation.
void LogCatalogNativeFrames(WindowCatalog& catalog, char const* phase) {
  char const* const names[4] = {"main", "child0", "child1", "child2"};
  for (int i = 0; i < 4; ++i) {
    auto& presenter = i == 0 ? LinuxMainWindow(catalog)
                            : *LinuxChildForRow(catalog, i - 1);
    FrameExpect frame{};
    if (!ReadNativeFrame(presenter, frame)) {
      FailCc("fatal: cannot snapshot mapped catalog window\n");
    }
    std::cerr << "WINDOW_CATALOG_LINUX_FRAME " << phase << ' ' << names[i]
              << ' ' << frame.x << ' ' << frame.y << ' ' << frame.w << ' '
              << frame.h << '\n';
  }
}

void VerifyGeomZCycle1ColdThenMove(WindowCatalog& catalog) {
  LogCatalogNativeFrames(catalog, "geom_loaded");
  if (!CatalogDesktopZOrderMatchesNative(catalog)) {
    FailCc("fatal: cold catalog native z-order differs from saved model\n");
  }
  LogMarker("WINDOW_CATALOG_LINUX_GEOM_Z_COLD_OK");
  ApplyGeomZCycle2WhenSettled(catalog);
}

void VerifyGeomZCycle2ColdThenClose(WindowCatalog& catalog) {
  LogCatalogNativeFrames(catalog, "geom_loaded");
  if (!CatalogDesktopZOrderMatchesNative(catalog)) {
    FailCc("fatal: second cold catalog native z-order differs from saved model\n");
  }
  LogMarker("WINDOW_CATALOG_LINUX_GEOM_Z_CYCLE2_COLD_OK");
  CloseMain(catalog);
}

void MoveAllCatalogWindowsNative(WindowCatalog& catalog, FrameExpect main,
                                 FrameExpect const* children) {
  MoveResizeNative(LinuxMainWindow(catalog).TopLevelWindow(), main);
  for (int i = 0; i < 3; ++i) {
    MoveResizeNative(LinuxChildForRow(catalog, i)->TopLevelWindow(),
                     children[i]);
  }
}

constexpr int kSettlePollMs = 100;
constexpr int kSettlePollLimit = 100;
constexpr int kSettleStablePolls = 5;

struct SettlePoll {
  WindowCatalog* catalog = nullptr;
  void (*then)(WindowCatalog&) = nullptr;
  int polls = 0;
  int stable = 0;
  FrameExpect last[4]{};
};

gboolean PollCatalogWindowsSettled(gpointer data) {
  auto* poll = static_cast<SettlePoll*>(data);
  FrameExpect raw[4]{};
  bool const readable = ReadCatalogRawFrames(*poll->catalog, raw);
  bool same = readable && CatalogAllOpenWindowsNativeMirrorSynced(*poll->catalog);
  for (int i = 0; readable && i < 4; ++i) {
    same = same && FrameMatchesExpect(raw[i], poll->last[i]);
    poll->last[i] = raw[i];
  }
  poll->stable = same ? poll->stable + 1 : 0;
  if (poll->stable < kSettleStablePolls) {
    if (++poll->polls >= kSettlePollLimit) {
      for (int i = -1; i < 3; ++i) {
        ui::linux::LinuxDesktopWindowPresenter* const presenter =
            i < 0 ? &LinuxMainWindow(*poll->catalog)
                  : LinuxChildForRow(*poll->catalog, i);
        FrameExpect frame{};
        bool const ok = presenter != nullptr && ReadRawClientFrame(*presenter, frame);
        std::cerr << "settle window " << i << " presenter="
                  << (presenter != nullptr) << " readable=" << ok << " frame "
                  << frame.x << ' ' << frame.y << ' ' << frame.w << ' '
                  << frame.h << '\n';
      }
      FailCc("fatal: catalog native windows never settled\n");
    }
    return G_SOURCE_CONTINUE;
  }
  poll->then(*poll->catalog);
  delete poll;
  return G_SOURCE_REMOVE;
}

// Window-manager placement after map does not publish when it matches the
// model, so settling is observed by polling the native windows.
void WhenCatalogWindowsSettled(WindowCatalog& catalog,
                               void (*then)(WindowCatalog&)) {
  auto* poll = new SettlePoll{};
  poll->catalog = &catalog;
  poll->then = then;
  g_timeout_add(kSettlePollMs, &PollCatalogWindowsSettled, poll);
}

void CountNativeRowSelected(GtkListBox*, GtkListBoxRow*, gpointer) {
  ++State().cc_selection_signals;
}

void BeginCcListIdentity(WindowCatalog& catalog) {
  GtkListBox* const box = GTK_LIST_BOX(LinuxList(catalog).ListWidget());
  for (int i = 0; i < 3; ++i) {
    GtkListBoxRow* const row = gtk_list_box_get_row_at_index(box, i);
    g_object_set_data(G_OBJECT(row), kCcRowMarker, GINT_TO_POINTER(i + 1));
  }
  catalog.persisted.list.Load();
  State().cc_selection_index = catalog.persisted.list->selection_index;
  State().cc_selection_signals = 0;
  State().cc_selection_handler = g_signal_connect(
      box, "row-selected", G_CALLBACK(CountNativeRowSelected), nullptr);
  MoveAllCatalogWindowsNative(catalog, kCcIdentityMain, kCcIdentityChild);
  State().step = AutomationStep::CcAwaitIdentityFrames;
}

void FinishCcListIdentity(WindowCatalog& catalog) {
  GtkListBox* const box = GTK_LIST_BOX(LinuxList(catalog).ListWidget());
  for (int i = 0; i < 3; ++i) {
    GtkListBoxRow* const row = gtk_list_box_get_row_at_index(box, i);
    if (row == nullptr ||
        g_object_get_data(G_OBJECT(row), kCcRowMarker) != GINT_TO_POINTER(i + 1)) {
      FailCc("fatal: geometry publication recreated GTK list rows\n");
    }
  }
  if (State().cc_selection_signals != 0) {
    FailCc("fatal: geometry publication re-emitted native list selection\n");
  }
  catalog.persisted.list.Load();
  if (catalog.persisted.list->selection_index != State().cc_selection_index) {
    FailCc("fatal: geometry publication changed model list selection\n");
  }
  g_signal_handler_disconnect(box, State().cc_selection_handler);
  State().cc_selection_handler = 0;
  LogMarker("WINDOW_CATALOG_LINUX_LIST_ROW_IDENTITY_OK");
}

void FinishCcSaveFrames(WindowCatalog& catalog) {
  FrameExpect raw[4]{};
  if (!ReadCatalogRawFrames(catalog, raw)) {
    FailCc("fatal: child-close save could not read native frames\n");
  }
  if (!CatalogFramesNear(catalog, kCcFinalMain, kCcFinalChild)) {
    FailCc("fatal: child-close save model geometry changed after settle\n");
  }
  LogCatalogRawFrames(raw);
  LogMarker("WINDOW_CATALOG_LINUX_CHILD_CLOSE_FRAMES_MODEL_OK");
  ClickButtonNative(LinuxLanguage(catalog));
  State().step = AutomationStep::CcAwaitLanguage;
}

void MoveAndCloseCcChild(WindowCatalog& catalog) {
  int const row = State().cc_iteration % 3;
  State().cc_row = row;
  ui::linux::LinuxDesktopWindowPresenter* child = LinuxChildForRow(catalog, row);
  if (child == nullptr) {
    FailCc("fatal: child-close iteration expected open child\n");
  }
  MoveResizeNative(child->TopLevelWindow(), kCcCloseFrames[State().cc_iteration]);
  ui::linux::LinuxDesktopWindowPresenter* sibling =
      LinuxChildForRow(catalog, (row + 1) % 3);
  FrameExpect sibling_frame = kCcCloseFrames[State().cc_iteration];
  sibling_frame.x += 40;
  sibling_frame.y += 40;
  MoveResizeNative(sibling->TopLevelWindow(), sibling_frame);
  CloseWindowNative(*child);
  State().step = AutomationStep::CcAwaitClosed;
}

void HandleChildCloseIncremental(WindowCatalog& catalog) {
  switch (State().step) {
    case AutomationStep::CcAwaitRows: {
      std::size_t const rows = RowCount(catalog);
      if (rows < 3) {
        if (rows == static_cast<std::size_t>(State().cc_row)) {
          return;
        }
        State().cc_row = static_cast<int>(rows);
        ClickButtonNative(LinuxAdd(catalog));
        return;
      }
      State().cc_row = 0;
      SelectListRowNative(LinuxList(catalog), 0);
      State().step = AutomationStep::CcAwaitSelect;
      return;
    }
    case AutomationStep::CcAwaitSelect:
      catalog.persisted.list.Load();
      if (catalog.persisted.list->selection_index != State().cc_row) {
        return;
      }
      ActivateListRowNative(LinuxList(catalog), State().cc_row);
      State().step = AutomationStep::CcAwaitOpen;
      return;
    case AutomationStep::CcAwaitOpen:
      if (LinuxChildForRow(catalog, State().cc_row) == nullptr) {
        return;
      }
      if (State().cc_row < 2) {
        ++State().cc_row;
        SelectListRowNative(LinuxList(catalog), State().cc_row);
        State().step = AutomationStep::CcAwaitSelect;
        return;
      }
      LogMarker("WINDOW_CATALOG_LINUX_CHILD_CLOSE_THREE_OPEN");
      LogMarker("WINDOW_CATALOG_LINUX_ROW_ACTIVATED_OK");
      State().cc_iteration = 0;
      MoveAndCloseCcChild(catalog);
      return;
    case AutomationStep::CcAwaitIdentityFrames:
      if (!CatalogFramesNear(catalog, kCcIdentityMain, kCcIdentityChild)) {
        return;
      }
      FinishCcListIdentity(catalog);
      MoveAllCatalogWindowsNative(catalog, kCcFinalMain, kCcFinalChild);
      State().step = AutomationStep::CcAwaitFinalFrames;
      return;
    case AutomationStep::CcAwaitClosed: {
      int const row = State().cc_row;
      if (LinuxChildForRow(catalog, row) != nullptr) {
        return;
      }
      if (LinuxMainWindow(catalog).TopLevelWindow() == nullptr ||
          LinuxChildForRow(catalog, (row + 1) % 3) == nullptr ||
          LinuxChildForRow(catalog, (row + 2) % 3) == nullptr) {
        FailCc("fatal: child close affected main or sibling windows\n");
      }
      catalog.persisted.list.Load();
      if (catalog.persisted.list->selection_index == row) {
        ClickButtonNative(LinuxOpen(catalog));
        State().step = AutomationStep::CcAwaitReopened;
        return;
      }
      SelectListRowNative(LinuxList(catalog), row);
      State().step = AutomationStep::CcAwaitReselect;
      return;
    }
    case AutomationStep::CcAwaitReselect:
      catalog.persisted.list.Load();
      if (catalog.persisted.list->selection_index != State().cc_row) {
        return;
      }
      ClickButtonNative(LinuxOpen(catalog));
      State().step = AutomationStep::CcAwaitReopened;
      return;
    case AutomationStep::CcAwaitReopened:
      if (LinuxChildForRow(catalog, State().cc_row) == nullptr) {
        return;
      }
      ++State().cc_iteration;
      if (State().cc_iteration < kCcIterations) {
        MoveAndCloseCcChild(catalog);
        return;
      }
      LogMarker("WINDOW_CATALOG_LINUX_CHILD_CLOSE_REOPEN_OK");
      State().step = AutomationStep::Idle;
      WhenCatalogWindowsSettled(catalog, &BeginCcListIdentity);
      return;
    case AutomationStep::CcAwaitFinalFrames:
      if (!CatalogFramesNear(catalog, kCcFinalMain, kCcFinalChild)) {
        return;
      }
      State().step = AutomationStep::Idle;
      WhenCatalogWindowsSettled(catalog, &FinishCcSaveFrames);
      return;
    case AutomationStep::CcAwaitLanguage:
      if (!LocaleIs(catalog, 1) || !AddLabelIs(catalog, "Добавить")) {
        return;
      }
      LogMarker("WINDOW_CATALOG_LINUX_CHILD_CLOSE_SAVE_READY");
      CloseMain(catalog);
      State().step = AutomationStep::Idle;
      return;
    default:
      return;
  }
}

void FinishChildCloseCold(WindowCatalog& catalog) {
  FrameExpect raw[4]{};
  if (!ReadCatalogRawFrames(catalog, raw)) {
    FailCc("fatal: child-close cold could not read native frames\n");
  }
  LogCatalogRawFrames(raw);
  if (!CatalogFramesNear(catalog, kCcFinalMain, kCcFinalChild)) {
    FailCc("fatal: child-close cold model geometry changed after placement\n");
  }
  LogMarker("WINDOW_CATALOG_LINUX_CHILD_CLOSE_COLD_OK");
  CloseMain(catalog);
}

void VerifyChildCloseCold(WindowCatalog& catalog) {
  if (RowCount(catalog) != 3) {
    FailCc("fatal: child-close cold expected three rows\n");
  }
  if (!LocaleIs(catalog, 1) || !AddLabelIs(catalog, "Добавить")) {
    FailCc("fatal: child-close cold expected Russian locale\n");
  }
  if (!CatalogFramesNear(catalog, kCcFinalMain, kCcFinalChild)) {
    FailCc("fatal: child-close cold model geometry not restored\n");
  }
  WhenCatalogWindowsSettled(catalog, &FinishChildCloseCold);
}

struct NativeGeometryPoll {
  WindowCatalog* catalog;
  int polls = 0;
  int stable = 0;
};

gboolean PollNativeGeometry(gpointer data) {
  auto* poll = static_cast<NativeGeometryPoll*>(data);
  if (++poll->polls > kSettlePollLimit) {
    FailCc("fatal: native initial placement did not settle\n");
  }
  auto& catalog = *poll->catalog;
  auto& presenter = LinuxMainWindow(catalog);
  GtkWidget* const widget = presenter.TopLevelWindow();
  auto& window = *catalog.persisted.main_window;
  if (!gtk_widget_get_mapped(widget)) return G_SOURCE_CONTINUE;
  if (!window.HasExplicitPosition() ||
      !CatalogWindowNativeMatchesMirror(window)) {
    poll->stable = 0;
    return G_SOURCE_CONTINUE;
  }
  if (++poll->stable < kSettleStablePolls) return G_SOURCE_CONTINUE;
  LogMarker("WINDOW_CATALOG_LINUX_OS_PLACEMENT_EVENT_OK");
  CloseMain(catalog);
  delete poll;
  return G_SOURCE_REMOVE;
}

}  // namespace

LinuxWindowCatalogSessionAutomation ParseLinuxWindowCatalogSessionAutomation(
    std::string_view value) {
  if (value == "native_geometry") {
    return LinuxWindowCatalogSessionAutomation::NativeGeometry;
  }
  if (value == "l3_save") {
    return LinuxWindowCatalogSessionAutomation::L3Save;
  }
  if (value == "l3_cold_verify") {
    return LinuxWindowCatalogSessionAutomation::L3ColdVerify;
  }
  if (value == "l3_cycle2_save") {
    return LinuxWindowCatalogSessionAutomation::L3Cycle2Save;
  }
  if (value == "l3_cycle2_cold_verify") {
    return LinuxWindowCatalogSessionAutomation::L3Cycle2ColdVerify;
  }
  if (value == "l3_geom_z_save") {
    return LinuxWindowCatalogSessionAutomation::L3GeomZSave;
  }
  if (value == "l3_geom_z_cold") {
    return LinuxWindowCatalogSessionAutomation::L3GeomZCold;
  }
  if (value == "l3_geom_z_cycle2_save") {
    return LinuxWindowCatalogSessionAutomation::L3GeomZCycle2Save;
  }
  if (value == "l3_geom_z_cycle2_cold") {
    return LinuxWindowCatalogSessionAutomation::L3GeomZCycle2Cold;
  }
  if (value == "l3_child_close_save") {
    return LinuxWindowCatalogSessionAutomation::L3ChildCloseSave;
  }
  if (value == "l3_child_close_cold") {
    return LinuxWindowCatalogSessionAutomation::L3ChildCloseCold;
  }
  WriteFatalStderr("fatal: unknown --session-automation value\n");
  std::abort();
}

LinuxWindowCatalogSessionAutomationHooks
MakeLinuxWindowCatalogSessionAutomationHooks(
    LinuxWindowCatalogSessionAutomation mode,
    std::function<void()> /*request_stop*/,
    ApplicationModelRuntime* runtime) {
  State().mode = mode;
  State().step = AutomationStep::Idle;
  State().runtime = runtime;
  State().first_open_widget = nullptr;
  State().cycle1_snapshotted = false;
  State().cycle2_snapshotted = false;
  return LinuxWindowCatalogSessionAutomationHooks{};
}

void LinuxWindowCatalogSessionAutomationHooks::OnAfterInitial(ae::Obj& ui_root) {
  LogMarker("WINDOW_CATALOG_LINUX_INITIAL_RENDER_OK");
  if (State().mode == LinuxWindowCatalogSessionAutomation::None) {
    return;
  }

  auto& catalog = AsCatalog(ui_root);
  if (State().mode == LinuxWindowCatalogSessionAutomation::NativeGeometry) {
    if (catalog.persisted.main_window->HasExplicitPosition()) {
      FailCc("fatal: initial graph must leave placement to the window manager\n");
    }
    g_timeout_add(kSettlePollMs, &PollNativeGeometry,
                  new NativeGeometryPoll{&catalog});
    return;
  }
  if (State().mode == LinuxWindowCatalogSessionAutomation::L3Save) {
    if (RowCount(catalog) != 0) {
      FailCc("fatal: fresh catalog must start with zero rows\n");
    }
    ClickButtonNative(LinuxAdd(catalog));
    State().step = AutomationStep::SaveAwaitAdd1;
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3ColdVerify) {
    if (RowCount(catalog) != 2) {
      WriteFatalStderr("fatal: cold verify expected two catalog rows\n");
      std::abort();
    }
    if (!LocaleIs(catalog, 0)) {
      WriteFatalStderr("fatal: cold verify expected English locale\n");
      std::abort();
    }
    LogMarker("WINDOW_CATALOG_LINUX_COLD_STATE_OK");
    State().step = AutomationStep::ColdAwaitAdd;
    ClickButtonNative(LinuxAdd(catalog));
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3Cycle2Save) {
    if (RowCount(catalog) != 3) {
      WriteFatalStderr("fatal: cycle2 save expected three rows from prior USER\n");
      std::abort();
    }
    ClickButtonNative(LinuxAdd(catalog));
    State().step = AutomationStep::Cycle2SaveAwaitAdd;
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3Cycle2ColdVerify) {
    if (RowCount(catalog) != 4) {
      WriteFatalStderr("fatal: cycle2 cold expected four rows\n");
      std::abort();
    }
    LogMarker("WINDOW_CATALOG_LINUX_CYCLE2_COLD_OK");
    CloseMain(catalog);
    State().step = AutomationStep::Idle;
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3GeomZSave) {
    ClickButtonNative(LinuxAdd(catalog));
    State().step = AutomationStep::GeomZAdd1;
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3ChildCloseSave) {
    State().cc_row = 0;
    ClickButtonNative(LinuxAdd(catalog));
    State().step = AutomationStep::CcAwaitRows;
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3ChildCloseCold) {
    VerifyChildCloseCold(catalog);
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3GeomZCold) {
    RequireThreeCatalogRows(catalog);
    State().step = AutomationStep::Idle;
    WhenCatalogWindowsSettled(catalog, &VerifyGeomZCycle1ColdThenMove);
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3GeomZCycle2Cold) {
    RequireThreeCatalogRows(catalog);
    State().step = AutomationStep::Idle;
    WhenCatalogWindowsSettled(catalog, &VerifyGeomZCycle2ColdThenClose);
  }
}

void LinuxWindowCatalogSessionAutomationHooks::OnAfterIncremental(
    ae::Obj& ui_root) {
  if (State().mode == LinuxWindowCatalogSessionAutomation::None) {
    return;
  }

  auto& catalog = AsCatalog(ui_root);

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3Save) {
    switch (State().step) {
      case AutomationStep::SaveAwaitAdd1:
        if (RowCount(catalog) != 1) {
          return;
        }
        ClickButtonNative(LinuxAdd(catalog));
        State().step = AutomationStep::SaveAwaitAdd2;
        return;
      case AutomationStep::SaveAwaitAdd2:
        if (RowCount(catalog) != 2) {
          return;
        }
        ClickButtonNative(LinuxAdd(catalog));
        State().step = AutomationStep::SaveAwaitAdd3;
        return;
      case AutomationStep::SaveAwaitAdd3:
        if (RowCount(catalog) != 3) {
          return;
        }
        SelectListRowNative(LinuxList(catalog), 0);
        State().step = AutomationStep::SaveAwaitSelectOpen;
        return;
      case AutomationStep::SaveAwaitSelectOpen:
        catalog.persisted.list.Load();
        if (!catalog.persisted.list || catalog.persisted.list->selection_index != 0) {
          return;
        }
        ClickButtonNative(LinuxOpen(catalog));
        State().step = AutomationStep::SaveAwaitFirstOpen;
        return;
      case AutomationStep::SaveAwaitFirstOpen: {
        ui::linux::LinuxDesktopWindowPresenter* child =
            LinuxChildForRow(catalog, 0);
        if (child == nullptr || child->TopLevelWindow() == nullptr) {
          return;
        }
        State().first_open_widget = child->TopLevelWindow();
        ClickButtonNative(LinuxOpen(catalog));
        State().step = AutomationStep::SaveAwaitReopenSame;
        return;
      }
      case AutomationStep::SaveAwaitReopenSame: {
        ui::linux::LinuxDesktopWindowPresenter* child =
            LinuxChildForRow(catalog, 0);
        if (child == nullptr || child->TopLevelWindow() != State().first_open_widget) {
          return;
        }
        LogMarker("WINDOW_CATALOG_LINUX_REOPEN_SAME_NATIVE_OK");
        CloseWindowNative(*child);
        State().step = AutomationStep::SaveAwaitCloseChild;
        return;
      }
      case AutomationStep::SaveAwaitCloseChild:
        if (LinuxChildForRow(catalog, 0) != nullptr) {
          return;
        }
        ClickButtonNative(LinuxOpen(catalog));
        State().step = AutomationStep::SaveAwaitReopenAfterClose;
        return;
      case AutomationStep::SaveAwaitReopenAfterClose:
        if (LinuxChildForRow(catalog, 0) == nullptr) {
          return;
        }
        ClickButtonNative(LinuxLanguage(catalog));
        State().step = AutomationStep::SaveAwaitLanguageRu;
        return;
      case AutomationStep::SaveAwaitLanguageRu:
        if (!LocaleIs(catalog, 1) || !AddLabelIs(catalog, "Добавить")) {
          return;
        }
        ClickButtonNative(LinuxLanguage(catalog));
        State().step = AutomationStep::SaveAwaitLanguageEn;
        return;
      case AutomationStep::SaveAwaitLanguageEn:
        if (!LocaleIs(catalog, 0) || !AddLabelIs(catalog, "Add")) {
          return;
        }
        LogMarker("WINDOW_CATALOG_LINUX_LOCALE_TOGGLE_OK");
        SelectListRowNative(LinuxList(catalog), 1);
        State().step = AutomationStep::SaveAwaitSelectDelete;
        return;
      case AutomationStep::SaveAwaitSelectDelete:
        catalog.persisted.list.Load();
        if (!catalog.persisted.list || catalog.persisted.list->selection_index != 1) {
          return;
        }
        ClickButtonNative(LinuxDelete(catalog));
        State().step = AutomationStep::SaveAwaitDelete;
        return;
      case AutomationStep::SaveAwaitDelete:
        if (RowCount(catalog) != 2) {
          return;
        }
        LogMarker("WINDOW_CATALOG_LINUX_SAVE_READY");
        CloseMain(catalog);
        State().step = AutomationStep::Idle;
        return;
      default:
        return;
    }
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3ColdVerify) {
    if (State().step == AutomationStep::ColdAwaitAdd &&
        RowCount(catalog) == 3) {
      LogMarker("WINDOW_CATALOG_LINUX_POST_COMMAND_OK");
      CloseMain(catalog);
      State().step = AutomationStep::Idle;
    }
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3Cycle2Save) {
    if (State().step == AutomationStep::Cycle2SaveAwaitAdd &&
        RowCount(catalog) == 4) {
      LogMarker("WINDOW_CATALOG_LINUX_CYCLE2_SAVE_READY");
      CloseMain(catalog);
      State().step = AutomationStep::Idle;
    }
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3GeomZSave ||
      State().mode == LinuxWindowCatalogSessionAutomation::L3GeomZCold) {
    HandleGeomZIncremental(catalog);
  }
  if (State().mode == LinuxWindowCatalogSessionAutomation::L3ChildCloseSave) {
    HandleChildCloseIncremental(catalog);
  }
}

}  // namespace apptraverse::example::surfaces
