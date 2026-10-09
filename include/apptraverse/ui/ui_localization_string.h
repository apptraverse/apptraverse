#ifndef APPTRAVERSE_UI_UI_LOCALIZATION_STRING_H_
#define APPTRAVERSE_UI_UI_LOCALIZATION_STRING_H_

#include <cstddef>
#include <string>
#include <vector>

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_string.h"

namespace apptraverse::ui {

enum class UiLanguage : int {
  kEnglish = 0,
  kRussian = 1,
  kCount = 2,
};

inline int UiLanguageCount() {
  return static_cast<int>(UiLanguage::kCount);
}

class UiLocalizationString : public NodeFor<UiLocalizationString> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiLocalizationString",
                           UiLocalizationString, Node, 0)

 protected:
  UiLocalizationString() = default;

 public:
  explicit UiLocalizationString(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(strings))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, strings);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, strings);
  }

  std::vector<UiString::ptr> strings;

  std::string Resolve(int language_index) const;
};

std::string ResolveLocalization(UiLocalizationString const* localization,
                                int language_index);

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_LOCALIZATION_STRING_H_
