#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_CONTROLS_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_CONTROLS_H_

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::example::minimal {

constexpr std::uint32_t kMinimalApplyButtonControlId = 7101u;
constexpr std::uint32_t kMinimalRowListControlId = 7102u;

ae::ObjId MinimalLocaleId();
ae::ObjId MinimalWindowTitleId();
ae::ObjId MinimalApplyButtonId();
ae::ObjId MinimalApplyButtonPresenterId();
ae::ObjId MinimalRowListId();
ae::ObjId MinimalRowListPresenterId();

void EnsureMinimalAppControlsRegistration();

class MinimalAppList : public NodeFor<MinimalAppList, ui::UiListContainer> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::minimal::MinimalAppList",
                           MinimalAppList, ui::UiListContainer, 0)

 protected:
  MinimalAppList() = default;

 public:
  explicit MinimalAppList(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_title))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_title);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_title);
  }

  ui::UiLocalizationString::ptr window_title;

  void HandleListRowActivated(std::size_t row_index) override;
};

class MinimalApplyButton : public NodeFor<MinimalApplyButton, ui::UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::minimal::MinimalApplyButton",
                           MinimalApplyButton, ui::UiPushButton, 0)

 protected:
  MinimalApplyButton() = default;

 public:
  explicit MinimalApplyButton(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_title), AE_MMBR(row_list))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_title, row_list);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_title, row_list);
  }

  ui::UiLocalizationString::ptr window_title;
  MinimalAppList::ptr row_list;

  void HandlePushButtonClicked() override;
};

class MinimalApplication;

ui::UiLocalizationString::ptr MakeMinimalLabel(ae::Domain& domain,
                                               char const* utf8);

void WireMinimalAppControlContext(MinimalApplication& application,
                                  ui::UiLocalizationString::ptr window_title);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_CONTROLS_H_
