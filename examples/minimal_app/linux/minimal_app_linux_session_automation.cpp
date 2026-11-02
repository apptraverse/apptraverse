#include "minimal_app_linux_session_automation.h"

#include <iostream>

#include "minimal_application.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

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

ui::UiListContainerPresenter& RowListPresenter(MinimalApplication& application) {
  application.presenters.row_list.Load();
  if (!application.presenters.row_list) {
    WriteFatalStderr("fatal: minimal app automation missing row list presenter\n");
    std::abort();
  }
  return static_cast<ui::UiListContainerPresenter&>(
      *application.presenters.row_list.operator->());
}

ui::UiPushButtonPresenter& ApplyPresenter(MinimalApplication& application) {
  application.presenters.apply_button.Load();
  if (!application.presenters.apply_button) {
    WriteFatalStderr("fatal: minimal app automation missing apply presenter\n");
    std::abort();
  }
  return static_cast<ui::UiPushButtonPresenter&>(
      *application.presenters.apply_button.operator->());
}

enum class AutomationStep {
  Idle,
  L3SaveAwaitApplyClick,
  L3SaveAwaitAppliedMirror,
  L3ColdAwaitRowSelection,
};

struct AutomationState {
  LinuxMinimalSessionAutomation mode = LinuxMinimalSessionAutomation::None;
  AutomationStep step = AutomationStep::Idle;
  std::function<void()> request_stop;
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
  WriteFatalStderr("fatal: unknown --session-automation value\n");
  std::abort();
}

LinuxMinimalSessionAutomationHooks MakeLinuxMinimalSessionAutomationHooks(
    LinuxMinimalSessionAutomation mode, std::function<void()> request_stop) {
  State().mode = mode;
  State().step = AutomationStep::Idle;
  State().request_stop = std::move(request_stop);
  return LinuxMinimalSessionAutomationHooks{};
}

void LinuxMinimalSessionAutomationHooks::OnAfterInitial(ae::Obj& ui_root) {
  LogMarker("MINIMAL_APP_INITIAL_RENDER_OK");
  if (State().mode == LinuxMinimalSessionAutomation::None) {
    return;
  }

  auto& application = static_cast<MinimalApplication&>(ui_root);
  if (State().mode == LinuxMinimalSessionAutomation::L3Save) {
    RowListPresenter(application).SubmitListRowSelectionChangedFromUi(1);
    State().step = AutomationStep::L3SaveAwaitApplyClick;
    return;
  }

  if (State().mode == LinuxMinimalSessionAutomation::L3ColdVerify) {
    if (!MirrorShowsAppliedState(application)) {
      WriteFatalStderr("fatal: cold reload mirror missing Applied state\n");
      std::abort();
    }
    LogMarker("MINIMAL_APP_L3_COLD_STATE_OK");
    RowListPresenter(application).SubmitListRowSelectionChangedFromUi(0);
    State().step = AutomationStep::L3ColdAwaitRowSelection;
  }
}

void LinuxMinimalSessionAutomationHooks::OnAfterIncremental(ae::Obj& ui_root) {
  if (State().mode == LinuxMinimalSessionAutomation::None) {
    return;
  }

  auto& application = static_cast<MinimalApplication&>(ui_root);
  if (State().mode == LinuxMinimalSessionAutomation::L3Save) {
    if (State().step == AutomationStep::L3SaveAwaitApplyClick) {
      ApplyPresenter(application).SimulateClickForTest();
      State().step = AutomationStep::L3SaveAwaitAppliedMirror;
      return;
    }
    if (State().step == AutomationStep::L3SaveAwaitAppliedMirror) {
      if (!MirrorShowsAppliedState(application)) {
        return;
      }
      LogMarker("MINIMAL_APP_L3_SAVE_READY");
      if (State().request_stop) {
        State().request_stop();
      }
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
    if (State().request_stop) {
      State().request_stop();
    }
    State().step = AutomationStep::Idle;
  }
}

}  // namespace apptraverse::example::minimal
