#ifndef APPTRAVERSE_TESTS_WIN_UI_COMMAND_ROUTING_TEST_NODES_H_
#define APPTRAVERSE_TESTS_WIN_UI_COMMAND_ROUTING_TEST_NODES_H_

#include "aether-objects/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::ui::windows {
class WinDesktopWindowPresenter;
class WinListContainerPresenter;
class WinPushButtonPresenter;
}  // namespace apptraverse::ui::windows

namespace apptraverse::test {

class TestDisableOnClickButton
    : public NodeFor<TestDisableOnClickButton, ui::UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::TestDisableOnClickButton",
                           TestDisableOnClickButton, ui::UiPushButton, 0)

 protected:
  TestDisableOnClickButton() = default;

 public:
  explicit TestDisableOnClickButton(ae::ObjProp prop) : NodeFor{prop} {}

  void HandlePushButtonClicked() override;
};

class TestActivationList : public NodeFor<TestActivationList, ui::UiListContainer> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::TestActivationList",
                           TestActivationList, ui::UiListContainer, 0)

 protected:
  TestActivationList() = default;

 public:
  explicit TestActivationList(ae::ObjProp prop) : NodeFor{prop} {}

  std::int32_t last_activated{-1};

  void HandleListRowActivated(std::size_t row_index) override;
};

class CommandRoutingButtonRoot : public NodeFor<CommandRoutingButtonRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CommandRoutingButtonRoot",
                           CommandRoutingButtonRoot, Node, 0)

 protected:
  CommandRoutingButtonRoot() = default;

 public:
  explicit CommandRoutingButtonRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presented_window), AE_MMBR(window_presenter),
                    AE_MMBR(button), AE_MMBR(button_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, presented_window, window_presenter, button, button_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, presented_window, window_presenter, button, button_presenter);
  }

  ui::UiDesktopWindow::ptr presented_window;
  ae::ObjPtr<ui::windows::WinDesktopWindowPresenter> window_presenter;
  TestDisableOnClickButton::ptr button;
  ae::ObjPtr<ui::windows::WinPushButtonPresenter> button_presenter;
};

class CommandRoutingListRoot : public NodeFor<CommandRoutingListRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CommandRoutingListRoot",
                           CommandRoutingListRoot, Node, 0)

 protected:
  CommandRoutingListRoot() = default;

 public:
  explicit CommandRoutingListRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presented_window), AE_MMBR(window_presenter),
                    AE_MMBR(list), AE_MMBR(list_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, presented_window, window_presenter, list, list_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, presented_window, window_presenter, list, list_presenter);
  }

  ui::UiDesktopWindow::ptr presented_window;
  ae::ObjPtr<ui::windows::WinDesktopWindowPresenter> window_presenter;
  TestActivationList::ptr list;
  ae::ObjPtr<ui::windows::WinListContainerPresenter> list_presenter;
};

ae::ObjId CommandRoutingButtonRootId();
ae::ObjId CommandRoutingListRootId();

CommandRoutingButtonRoot::ptr BuildCommandRoutingButtonGraph(ae::Domain& domain);
CommandRoutingListRoot::ptr BuildCommandRoutingListGraph(ae::Domain& domain);

void EnsureCommandRoutingTestNodeRegistration();

// Headless mirror/proxy check without native HWND (shared test nodes).
void TestDisableButtonStructuralMirrorHeadless();
void TestActivationListStructuralMirrorHeadless();

}  // namespace apptraverse::test

#endif
