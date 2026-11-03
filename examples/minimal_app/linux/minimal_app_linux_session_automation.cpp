#include "minimal_app_linux_session_automation.h"

#include <iostream>

#include "minimal_application.h"
#include "minimal_app_controls.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/platform/linux/linux_ui_list_presenter.h"
#include "apptraverse/platform/linux/linux_ui_push_button_presenter.h"
#include "apptraverse/platform/linux/linux_ui_window_presenter.h"
#include "apptraverse/ui/ui_desktop_window.h"

#include <gtk/gtk.h>

namespace apptraverse::example::minimal {
namespace {

void LogMarker(char const* marker) {
  std::cerr << marker << '\n';
}

bool MirrorShowsAppliedState(MinimalApplication& application) {
  application.main_window.Load();
  application.apply_button.Load();
  application.row_list.Load();
  application.presenters.apply_button.Load();
  if (!application.main_window || !application.apply_button ||
      !application.row_list || !application.presenters.apply_button) {
    return false;
  }
  ui::UiDesktopWindow& window =
      static_cast<ui::UiDesktopWindow&>(*application.main_window.operator->());
  window.presenter.Load();
  if (!window.presenter || !window.presenter->title) {
    return false;
  }
  window.presenter->title.Load();
  if (!window.presenter->title) {
    return false;
  }
  if (window.presenter->title->Resolve(0) != "Applied") {
    return false;
  }
  if (application.apply_button->enabled()) {
    return false;
  }
  if (application.row_list->selection_index != -1) {
    return false;
  }
  return true;
}

ui::linux::LinuxListContainerPresenter& LinuxRowList(MinimalApplication& app) {
  app.presenters.row_list.Load();
  if (!app.presenters.row_list) {
    WriteFatalStderr("fatal: minimal app automation missing row list presenter\n");
    std::abort();
  }
  return static_cast<ui::linux::LinuxListContainerPresenter&>(
      *app.presenters.row_list.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxApply(MinimalApplication& app) {
  app.presenters.apply_button.Load();
  if (!app.presenters.apply_button) {
    WriteFatalStderr("fatal: minimal app automation missing apply presenter\n");
    std::abort();
  }
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *app.presenters.apply_button.operator->());
}

ui::linux::LinuxPushButtonPresenter& LinuxAuxiliaryOpener(MinimalApplication& app) {
  app.presenters.auxiliary_opener.Load();
  if (!app.presenters.auxiliary_opener) {
    WriteFatalStderr(
        "fatal: minimal app automation missing auxiliary opener presenter\n");
    std::abort();
  }
  return static_cast<ui::linux::LinuxPushButtonPresenter&>(
      *app.presenters.auxiliary_opener.operator->());
}

ui::linux::LinuxDesktopWindowPresenter& LinuxMainWindowPresenter(
    MinimalApplication& app) {
  app.main_window.Load();
  app.main_window->presenter.Load();
  if (!app.main_window || !app.main_window->presenter) {
    WriteFatalStderr("fatal: minimal app automation missing main window presenter\n");
    std::abort();
  }
  return static_cast<ui::linux::LinuxDesktopWindowPresenter&>(
      *app.main_window->presenter.operator->());
}

ui::linux::LinuxDesktopWindowPresenter* LinuxAuxiliaryWindowPresenter(
    MinimalApplication& app) {
  if (!app.auxiliary_presented_open) {
    return nullptr;
  }
  app.auxiliary.window.Load();
  app.auxiliary.window->presenter.Load();
  if (!app.auxiliary.window || !app.auxiliary.window->presenter) {
    return nullptr;
  }
  return &static_cast<ui::linux::LinuxDesktopWindowPresenter&>(
      *app.auxiliary.window->presenter.operator->());
}

void SelectListRowNative(ui::linux::LinuxListContainerPresenter& list,
                         int index) {
  GtkWidget* widget = list.ListWidget();
  if (widget == nullptr) {
    WriteFatalStderr("fatal: automation list widget missing\n");
    std::abort();
  }
  GtkListBox* box = GTK_LIST_BOX(widget);
  GtkListBoxRow* row = gtk_list_box_get_row_at_index(box, index);
  if (row == nullptr) {
    WriteFatalStderr("fatal: automation list row missing\n");
    std::abort();
  }
  gtk_list_box_select_row(box, row);
}

void ClickButtonNative(ui::linux::LinuxPushButtonPresenter& button) {
  GtkWidget* widget = button.ButtonWidget();
  if (widget == nullptr) {
    WriteFatalStderr("fatal: automation button widget missing\n");
    std::abort();
  }
  gtk_button_clicked(GTK_BUTTON(widget));
}

void CloseWindowNative(ui::linux::LinuxDesktopWindowPresenter& window) {
  GtkWidget* widget = window.TopLevelWindow();
  if (widget == nullptr) {
    WriteFatalStderr("fatal: automation window widget missing\n");
    std::abort();
  }
  gtk_window_close(GTK_WINDOW(widget));
}

enum class AutomationStep {
  Idle,
  L3SaveAwaitApplyClick,
  L3SaveAwaitAppliedMirror,
  L3ColdAwaitRowSelection,
  L3DetailAwaitOpen,
  L3DetailAwaitNativeClose,
  L3DetailAwaitClosedMirror,
};

struct AutomationState {
  LinuxMinimalSessionAutomation mode = LinuxMinimalSessionAutomation::None;
  AutomationStep step = AutomationStep::Idle;
  ApplicationModelRuntime* runtime = nullptr;
};

AutomationState& State() {
  static AutomationState state;
  return state;
}

}  // namespace

LinuxMinimalSessionAutomation ParseLinuxMinimalSessionAutomation(
    std::string_view value) {
  if (value == "l3_save") {
    return LinuxMinimalSessionAutomation::L3Save;
  }
  if (value == "l3_cold_verify") {
    return LinuxMinimalSessionAutomation::L3ColdVerify;
  }
  if (value == "l3_detail_close") {
    return LinuxMinimalSessionAutomation::L3DetailClose;
  }
  WriteFatalStderr("fatal: unknown --session-automation value\n");
  std::abort();
}

LinuxMinimalSessionAutomationHooks MakeLinuxMinimalSessionAutomationHooks(
    LinuxMinimalSessionAutomation mode, std::function<void()> /*request_stop*/,
    ApplicationModelRuntime* runtime) {
  State().mode = mode;
  State().step = AutomationStep::Idle;
  State().runtime = runtime;
  return LinuxMinimalSessionAutomationHooks{};
}

void LinuxMinimalSessionAutomationHooks::OnAfterInitial(ae::Obj& ui_root) {
  LogMarker("MINIMAL_APP_INITIAL_RENDER_OK");
  if (State().mode == LinuxMinimalSessionAutomation::None) {
    return;
  }

  auto& application = static_cast<MinimalApplication&>(ui_root);
  if (State().mode == LinuxMinimalSessionAutomation::L3Save) {
    SelectListRowNative(LinuxRowList(application), 1);
    State().step = AutomationStep::L3SaveAwaitApplyClick;
    return;
  }

  if (State().mode == LinuxMinimalSessionAutomation::L3ColdVerify) {
    if (!MirrorShowsAppliedState(application)) {
      WriteFatalStderr("fatal: cold reload mirror missing Applied state\n");
      std::abort();
    }
    LogMarker("MINIMAL_APP_L3_COLD_STATE_OK");
    SelectListRowNative(LinuxRowList(application), 0);
    State().step = AutomationStep::L3ColdAwaitRowSelection;
    return;
  }

  if (State().mode == LinuxMinimalSessionAutomation::L3DetailClose) {
    ClickButtonNative(LinuxAuxiliaryOpener(application));
    State().step = AutomationStep::L3DetailAwaitOpen;
  }
}

void LinuxMinimalSessionAutomationHooks::OnAfterIncremental(ae::Obj& ui_root) {
  if (State().mode == LinuxMinimalSessionAutomation::None) {
    return;
  }

  auto& application = static_cast<MinimalApplication&>(ui_root);
  if (State().mode == LinuxMinimalSessionAutomation::L3Save) {
    if (State().step == AutomationStep::L3SaveAwaitApplyClick) {
      ClickButtonNative(LinuxApply(application));
      State().step = AutomationStep::L3SaveAwaitAppliedMirror;
      return;
    }
    if (State().step == AutomationStep::L3SaveAwaitAppliedMirror) {
      if (!MirrorShowsAppliedState(application)) {
        return;
      }
      LogMarker("MINIMAL_APP_L3_SAVE_READY");
      CloseWindowNative(LinuxMainWindowPresenter(application));
      State().step = AutomationStep::Idle;
    }
    return;
  }

  if (State().mode == LinuxMinimalSessionAutomation::L3ColdVerify &&
      State().step == AutomationStep::L3ColdAwaitRowSelection) {
    application.row_list.Load();
    if (!application.row_list ||
        application.row_list->selection_index != 0) {
      return;
    }
    LogMarker("MINIMAL_APP_L3_POST_COMMAND_OK");
    CloseWindowNative(LinuxMainWindowPresenter(application));
    State().step = AutomationStep::Idle;
    return;
  }

  if (State().mode == LinuxMinimalSessionAutomation::L3DetailClose) {
    if (State().step == AutomationStep::L3DetailAwaitOpen) {
      if (!application.auxiliary_presented_open) {
        return;
      }
      ui::linux::LinuxDesktopWindowPresenter* aux =
          LinuxAuxiliaryWindowPresenter(application);
      if (aux == nullptr) {
        return;
      }
      CloseWindowNative(*aux);
      State().step = AutomationStep::L3DetailAwaitNativeClose;
      return;
    }
    if (State().step == AutomationStep::L3DetailAwaitNativeClose) {
      if (application.auxiliary_presented_open) {
        return;
      }
      LogMarker("MINIMAL_APP_L3_DETAIL_NATIVE_CLOSE_OK");
      CloseWindowNative(LinuxMainWindowPresenter(application));
      State().step = AutomationStep::Idle;
    }
  }
}

}  // namespace apptraverse::example::minimal
