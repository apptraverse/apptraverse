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
  catalog.presenters.list_presenter.Load();
  return static_cast<ui::linux::LinuxListContainerPresenter&>(
      *catalog.presenters.list_presenter.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxAdd(WindowCatalog& catalog) {
  catalog.presenters.add_button_presenter.Load();
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *catalog.presenters.add_button_presenter.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxOpen(WindowCatalog& catalog) {
  catalog.presenters.open_button_presenter.Load();
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *catalog.presenters.open_button_presenter.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxDelete(WindowCatalog& catalog) {
  catalog.presenters.delete_button_presenter.Load();
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *catalog.presenters.delete_button_presenter.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxLanguage(WindowCatalog& catalog) {
  catalog.presenters.language_button_presenter.Load();
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *catalog.presenters.language_button_presenter.operator->());
}

ui::linux::LinuxDesktopWindowPresenter& LinuxMainWindow(WindowCatalog& catalog) {
  catalog.main_window.Load();
  catalog.main_window->presenter.Load();
  return static_cast<ui::linux::LinuxDesktopWindowPresenter&>(
      *catalog.main_window->presenter.operator->());
}

ui::linux::LinuxDesktopWindowPresenter* LinuxChildForRow(WindowCatalog& catalog,
                                                         int row_index) {
  catalog.list.Load();
  if (!catalog.list ||
      row_index < 0 ||
      static_cast<std::size_t>(row_index) >= catalog.list->rows.size()) {
    return nullptr;
  }
  auto& holder = static_cast<WindowHolder&>(
      *catalog.list->rows[static_cast<std::size_t>(row_index)]);
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

void ClickButtonNative(ui::linux::LinuxPushButtonPresenter& button) {
  gtk_button_clicked(GTK_BUTTON(button.ButtonWidget()));
}

void CloseWindowNative(ui::linux::LinuxDesktopWindowPresenter& window) {
  gtk_window_close(GTK_WINDOW(window.TopLevelWindow()));
}

std::size_t RowCount(WindowCatalog& catalog) {
  catalog.list.Load();
  return catalog.list ? catalog.list->rows.size() : 0;
}

bool LocaleIs(WindowCatalog& catalog, int language_index) {
  catalog.locale.Load();
  return catalog.locale && catalog.locale->language_index() == language_index;
}

bool AddLabelIs(WindowCatalog& catalog, char const* expected) {
  catalog.texts.label_add.Load();
  return catalog.texts.label_add &&
         catalog.texts.label_add->Resolve(catalog.locale->language_index()) ==
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
  GeomZCycle2Apply,
  GeomZCycle2AwaitModel,
  GeomZCycle2ColdVerify,
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
  catalog.list.Load();
  catalog.main_window.Load();
  if (!FrameNearExpect(
          {catalog.main_window->x(), catalog.main_window->y(),
           catalog.main_window->client_width(),
           catalog.main_window->client_height()},
          kGeomZMain1)) {
    return false;
  }
  FrameExpect const child_frames[] = {kGeomZChild0_1, kGeomZChild1_1,
                                      kGeomZChild2_1};
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.list->rows[static_cast<std::size_t>(i)]);
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
  catalog.list.Load();
  catalog.main_window.Load();
  if (!CatalogWindowNativeMatchesMirror(*catalog.main_window)) {
    return false;
  }
  if (!catalog.list) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (LinuxChildForRow(catalog, i) == nullptr) {
      return false;
    }
    auto& holder = static_cast<WindowHolder&>(
        *catalog.list->rows[static_cast<std::size_t>(i)]);
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
  catalog.main_window.Load();
  if (!CatalogWindowMirrorFrame(*catalog.main_window, State().cycle1_main)) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.list->rows[static_cast<std::size_t>(i)]);
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
  catalog.main_window.Load();
  if (!CatalogWindowMirrorFrame(*catalog.main_window, State().cycle2_main)) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.list->rows[static_cast<std::size_t>(i)]);
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
  if (catalog.desktop_z_order.size() != native.size()) {
    return false;
  }
  for (std::size_t i = 0; i < native.size(); ++i) {
    if (!catalog.desktop_z_order[i].is_valid() ||
        catalog.desktop_z_order[i].id().id() != native[i]) {
      return false;
    }
  }
  return true;
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

void SnapshotCycle1ExpectationsFromMirror(WindowCatalog& catalog) {
  catalog.main_window.Load();
  SnapshotMirrorFrame(*catalog.main_window, State().cycle1_main);
  catalog.list.Load();
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.list->rows[static_cast<std::size_t>(i)]);
    holder.window.Load();
    SnapshotMirrorFrame(*holder.window, State().cycle1_child[i]);
  }
  State().cycle1_snapshotted = true;
}

void SnapshotCycle2ExpectationsFromMirror(WindowCatalog& catalog) {
  catalog.main_window.Load();
  SnapshotMirrorFrame(*catalog.main_window, State().cycle2_main);
  catalog.list.Load();
  for (int i = 0; i < 3; ++i) {
    auto& holder = static_cast<WindowHolder&>(
        *catalog.list->rows[static_cast<std::size_t>(i)]);
    holder.window.Load();
    SnapshotMirrorFrame(*holder.window, State().cycle2_child[i]);
  }
  State().cycle2_snapshotted = true;
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
      catalog.list.Load();
      if (!catalog.list || catalog.list->selection_index != 0) {
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
      catalog.list.Load();
      if (!catalog.list || catalog.list->selection_index != 1) {
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
      catalog.list.Load();
      if (!catalog.list || catalog.list->selection_index != 2) {
        return;
      }
      ClickButtonNative(LinuxOpen(catalog));
      if (LinuxChildForRow(catalog, 2) == nullptr) {
        return;
      }
      ApplyGeomZCycle1Native(catalog);
      FlushCatalogNativeFrames(catalog);
      SnapshotCycle1ExpectationsFromMirror(catalog);
      LogMarker("WINDOW_CATALOG_LINUX_GEOM_Z_MODEL_OK");
      CloseMain(catalog);
      State().step = AutomationStep::Idle;
      return;
    case AutomationStep::GeomZCycle2Apply:
      ApplyGeomZCycle2Native(catalog);
      State().step = AutomationStep::GeomZCycle2AwaitModel;
      return;
    case AutomationStep::GeomZCycle2AwaitModel:
      FlushCatalogNativeFrames(catalog);
      SnapshotCycle2ExpectationsFromMirror(catalog);
      LogMarker("WINDOW_CATALOG_LINUX_GEOM_Z_CYCLE2_SAVE_OK");
      CloseMain(catalog);
      State().step = AutomationStep::Idle;
      return;
    default:
      return;
  }
}

}  // namespace

LinuxWindowCatalogSessionAutomation ParseLinuxWindowCatalogSessionAutomation(
    std::string_view value) {
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
  if (State().mode == LinuxWindowCatalogSessionAutomation::L3Save) {
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

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3GeomZCold) {
    catalog.main_window.Load();
    if (catalog.main_window->x() == 80 && catalog.main_window->y() == 80) {
      WriteFatalStderr("fatal: cold load did not restore moved main geometry\n");
      std::abort();
    }
    SnapshotCycle1ExpectationsFromMirror(catalog);
    if (!catalog.desktop_z_order.empty() &&
        CatalogDesktopZOrderMatchesNative(catalog)) {
      LogMarker("WINDOW_CATALOG_LINUX_Z_ORDER_COLD_OK");
    }
    LogMarker("WINDOW_CATALOG_LINUX_GEOM_Z_COLD_OK");
    ApplyGeomZCycle2Native(catalog);
    State().step = AutomationStep::GeomZCycle2AwaitModel;
    return;
  }

  if (State().mode == LinuxWindowCatalogSessionAutomation::L3GeomZCycle2Cold) {
    catalog.main_window.Load();
    SnapshotCycle2ExpectationsFromMirror(catalog);
    if (!catalog.desktop_z_order.empty() &&
        CatalogDesktopZOrderMatchesNative(catalog)) {
      LogMarker("WINDOW_CATALOG_LINUX_Z_ORDER_CYCLE2_COLD_OK");
    }
    LogMarker("WINDOW_CATALOG_LINUX_GEOM_Z_CYCLE2_COLD_OK");
    CloseMain(catalog);
    State().step = AutomationStep::Idle;
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
        catalog.list.Load();
        if (!catalog.list || catalog.list->selection_index != 0) {
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
        catalog.list.Load();
        if (!catalog.list || catalog.list->selection_index != 1) {
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
}

}  // namespace apptraverse::example::surfaces
