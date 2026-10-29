#ifndef APPTRAVERSE_TESTS_WIN_UI_COMMAND_ROUTING_TEST_NODES_H_
#define APPTRAVERSE_TESTS_WIN_UI_COMMAND_ROUTING_TEST_NODES_H_

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_push_button.h"

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

void EnsureCommandRoutingTestNodeRegistration();

// Headless mirror/proxy check without native HWND (shared test nodes).
void TestDisableButtonStructuralMirrorHeadless();
void TestActivationListStructuralMirrorHeadless();

}  // namespace apptraverse::test

#endif
