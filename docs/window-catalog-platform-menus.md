# WindowCatalog: platform menus and system appearance

Decision: 2026-11-17. Windows implementation; other adapters are porting guidance,
not an implementation or runtime acceptance claim.

## User interface

Windows WindowCatalog has two ordinary application menu groups:

- Theme: From system, Light, Dark, Soft dark.
- Language: English, Русский (language endonyms).

There is no settings/demo window and no local font family, weight, text size,
high contrast or keyboard cue override. Text and layout follow the OS. The
language action is not presented as a button in Windows content. Other platform
supplies currently keep their existing content policy.

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
locale identity and its base belong to that LOCAL root in the Windows supply.
Language uses SetUiLanguageIndexEvent with the same LOCAL scope. No additional
file format, migration or profile compatibility is introduced.

Native input -> presenter/proxy -> CatalogUiState command -> model Event ->
publication -> UI mirror -> native menu. The presenter creates and updates HMENU
resources, resolves localized labels and projects checked states. It stores only
runtime rendering caches. Menu choices and command behavior belong to the model.
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
| macOS / AppKit | Native NSAppearance and semantic colors adapt controls. Custom surfaces/resources must also adapt. | Native-following composition: Language only. No need to add a theme override merely to follow the OS. |
| iOS / UIKit | Dynamic semantic colors and traits; Dynamic Type for system text preferences. | Native-following policy when ported; native menu/navigation host chosen separately. |
| Android / Views | DayNight resources/theme; system `sp` conversion for text, including nonlinear scaling. | Native-following policy where the adapter uses these mechanisms. Do not duplicate a text scale control. |
| Linux / GTK | GTK theme/style context and GtkSettings; availability depends on theme/desktop integration. | Choose policy for the actual adapter; do not infer native dark support from the OS name alone. |
| Web / native DOM | Declare `color-scheme`; preserve browser forced colors and form rendering; adapt authored page surfaces with CSS system colors/media queries. | Native-following if all surfaces adapt declaratively; otherwise adapter-applied theme. Use DOM menus with semantics/keyboard behavior when ported. |

A native-following backend can still implement application theme overrides if
separately requested. Automatic following alone does not justify exposing one.
The current task changes Windows execution only; future adapters consume the
same semantic definition and map it to their own native menu surfaces.

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
