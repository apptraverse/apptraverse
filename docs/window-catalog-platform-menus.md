# WindowCatalog: platform menus and system appearance

Windows, Linux and Android expose the shared appearance and language choices.
The macOS and iOS implementations expose Language through Swift and follow
system appearance. Web remains a future consumer of the same menu definition.

## User interface

Windows, GTK and Android WindowCatalog expose two application menu groups:

- Theme: From system, Light, Dark, Soft dark.
- Language: English, Русский (language endonyms).

The menu has no separate window or overrides for font family, weight, text size,
high contrast or keyboard cues. Text and layout follow the OS. On the ports
that use the menu, Language replaces the old content button.

The existing layout context menu remains a constraint-engine demonstration;
it is separate from language and appearance preferences.

## Model and supply

`CatalogThemeHandling` is a backend composition input. With
`kNativeFollowsSystem`, the distilled menu contains Language only. With
`kAdapterAppliesTheme`, it also contains Theme. This choice is made when authoring
the supply, not by reconstructing missing menu objects at runtime.

`CatalogMenuDefinition` holds localized group/item labels and semantic commands.
It is an immutable supply object. `CatalogUiState` extends the existing LOCAL
device root; appearance uses the existing UiDeviceState Events. The existing
locale identity and its base belong to that LOCAL root in menu-enabled supplies.
`SetUiLanguageIndexEvent` inherits the locale's scope. Portable USER transfer
copies only `user.bin`. The destination keeps its own `local/user.bin` and
continues observing its own OS. Use matching supplies for the S0 profiles.

Native input -> presenter/proxy -> CatalogUiState command -> model Event ->
publication -> UI mirror -> native menu. The presentation layer maps the same
labels, commands and selection to Win32 HMENU, GTK radio items, Android
submenus, AppKit NSMenu or SwiftUI Menu. Menu choices and command behavior
belong to the model.
Active language materialization releases inactive string slots; native handles
and rendering caches are not serialized.

Composition/builders are separate translation units from menu registration and
behavior. Embedded consumers reference only the latter; no runtime builder or
settings-window graph is required.

## Ownership of system behavior

Native controls retain the platform's accessibility, keyboard/input and contrast
behavior. AppTraverse supplies semantics, measures native controls and recalculates
its own layout. It reacts to actual system notifications and persists the LOCAL
environment baseline under the existing lifecycle contract.

For classic Win32, setting the system-derived HFONT on a button is necessary to
honor text settings not automatically applied by that control. Windows still
renders the text. This adapter work does not create a user-facing font preference.
Do not multiply a system font already adjusted for text size a second time.

System high contrast overrides all local appearance choices. Existing Windows
dark appearance still uses its existing limited owner-drawn button path; the
native-control distinction must not be confused with fully native dark rendering.
This change removes demo font/cue painting policy, not the existing dark palette.

## Porting decisions

| Adapter | How to follow system theme | Menu policy |
| --- | --- | --- |
| Windows / classic Win32 | Read changes; apply supported native appearance APIs and existing adapter palette. Win7 has no modern application dark preference. | Theme + Language. Auto uses only available OS facts. |
| macOS / AppKit | NSAppearance and semantic colors drive the WindowCatalog host. | Language only, in the Swift AppKit menu bar. |
| iOS / SwiftUI | Semantic colors follow system appearance. | Language only, in the SwiftUI toolbar. |
| Android / Views | Existing adapter palette follows the OS in Auto and keeps native controls. Text uses the Android scaling path. | Theme + Language in the Activity options menu. |
| Linux / GTK | GTK theme/style context and GtkSettings provide the system baseline. The adapter also supports explicit palettes. | Theme + Language in the GTK menu bar. |
| Web / native DOM | Declare `color-scheme`; preserve browser forced colors and form rendering; adapt authored page surfaces with CSS system colors/media queries. | Native-following if all surfaces adapt declaratively; otherwise adapter-applied theme. Use DOM menus with semantics/keyboard behavior when ported. |

The Apple UI is written in Swift. The existing bridge supplies localized menu
values and forwards command indices to the shared model. On macOS the desktop
session saves LOCAL at shutdown. On iOS the current lifecycle save writes the
same two partitions.

## Primary references

- [Microsoft: text scaling](https://learn.microsoft.com/en-us/windows/apps/develop/input/text-scaling)
- [Microsoft: WM_SETFONT](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-setfont)
- [Microsoft: Win32 dark/light themes](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes)
- [Microsoft: high contrast](https://learn.microsoft.com/en-us/windows/win32/controls/supporting-high-contrast-themes)
- [AppKit: NSAppearance](https://developer.apple.com/documentation/appkit/nsappearance)
- [UIKit: Dynamic Type](https://developer.apple.com/documentation/uikit/uicontentsizecategoryadjusting/adjustsfontforcontentsizecategory)
- [Android: dark theme](https://developer.android.com/develop/ui/views/theming/darktheme)
- [Android: nonlinear text scaling](https://developer.android.com/about/versions/14/features#non-linear-font-scaling)
- [GTK: settings](https://docs.gtk.org/gtk3/class.Settings.html)
- [W3C: CSS Color Adjustment](https://www.w3.org/TR/css-color-adjust-1/)
