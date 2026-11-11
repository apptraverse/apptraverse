#ifndef APPTRAVERSE_UI_UI_NATIVE_PRESENTER_SCHEMA_H_
#define APPTRAVERSE_UI_UI_NATIVE_PRESENTER_SCHEMA_H_

namespace apptraverse::ui::native_schema {

// One persistent role, one schema, one implementation per executable. Native
// handles and callbacks belong only to the C++ implementation. These layers
// serialize their common UI base and contain no platform-specific fields.
inline constexpr char kWindow[] = "apptraverse::ui::NativeWindowPresenter";
inline constexpr char kList[] = "apptraverse::ui::NativeListContainerPresenter";
inline constexpr char kPushButton[] = "apptraverse::ui::NativePushButtonPresenter";

}  // namespace apptraverse::ui::native_schema

#endif  // APPTRAVERSE_UI_UI_NATIVE_PRESENTER_SCHEMA_H_
