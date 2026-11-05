#include "window_catalog_linux_session_automation.h"

#include <iostream>

#include "window_catalog_model.h"

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
};

struct AutomationState {
  LinuxWindowCatalogSessionAutomation mode =
      LinuxWindowCatalogSessionAutomation::None;
  AutomationStep step = AutomationStep::Idle;
  ApplicationModelRuntime* runtime = nullptr;
  GtkWidget* first_open_widget = nullptr;
};

AutomationState& State() {
  static AutomationState state;
  return state;
}

void CloseMain(WindowCatalog& catalog) {
  CloseWindowNative(LinuxMainWindow(catalog));
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
  }
}

}  // namespace apptraverse::example::surfaces
