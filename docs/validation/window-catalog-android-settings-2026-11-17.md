# Android shared layout and LOCAL acceptance - 2026-11-17

Scope: existing `Aether_NDK_Smoke_x86_64`, Android14/API34, emulator-5554.
Toolchain: JDK20.0.1, SDK34/build-tools36, NDK29.0.14206865, SDK CMake4.1.2,
Debug host exporter v14314.44. Existing patched dependency sources were reused
offline. Fresh experimental S0 profiles and matching newly distilled supplies
were used; older profiles were retained. ARM64/devices, Apple and Release are
excluded.

Actual OS fingerprint:
`google/sdk_gphone64_x86_64/emu64xa:14/UE1A.230829.050/12077443:userdebug/dev-keys`.
No system-image replacement or OTA was performed. Density/font/night-mode changes
and restart acceptance do not establish OS-image-upgrade compatibility.

## Source boundary

Native controls measure text in SP. The shared AppTraverse solver runs beside the
UI mirror in Android logical dp; the backend converts its result to pixels once
and retains runtime rectangles across publications. Native scroll views expose the
solver's content extent; they do not solve control geometry. Native list rows are
internals of one list control. Required typed layout/device links come from generic
UiPresentedWindow composition; there are no backend WindowCatalog casts.

Synchronous parent/child construction may wait for children. The posted Java UI
dispatch strictly requires every layout leaf to be natively mounted. An absent
required binding aborts; there is no indefinite old-geometry fallback.

Auto/Light/Dark and dark softening use native menu commands through
ModelObjectProxy -> SetAppearance Event -> publication. Native theme attributes
supply colors and state feedback. Preferences are stored in LOCAL, with no INI.

The UI thread captures the real OS before startup. Model prepare compares it to
loaded LOCAL before the first publication. Live configuration dispatch, content,
display and accessibility observers use the same comparison path. Subscriptions
end with the native mount, with no OS polling. Supported observations are density,
font scale, night mode, native semantic colors, animator reduction,
touch-exploration hint and display endpoints/bounds/rotation/refresh. Unsupported
high-contrast/keyboard-cue settings are not claimed. Touch exploration is an OS
hint, not proof that a screen reader is present.

USER and LOCAL are separate ordinary RAM object stores over embedded BACKING.
The generic embedded session routes marked local roots before Load. onStop saves
`<profile>/user.bin` and `<profile>/local/user.bin` using the same native object
format. Normal Debug starts fresh. Load-only restores both. Optional
`localSeedSubdir` seeds only an absent LOCAL graph; existing LOCAL wins in full and
the supplied source path is not read.

## Executed transfer and compatible-update checks

A retained real Windows-native current-schema source contains USER7250bytes and
LOCAL4044bytes: two holders (Window1 closed, Window2 open), locale0, active main2,
next number3. Target-ABI Android inspection preserved holder/window identities,
open states, active window and locale in all three actual native scenarios:

- portable-only import into an Android-empty profile; Android created Auto LOCAL;
- initial optional Windows LOCAL seed; Dark/soft preferences survived while the
  Windows OS baseline adapted to the real Android startup environment;
- portable import after Android had already run; own LOCAL preferences and baseline
  remained intact even with an absent supplied seed path.

The target fixture checks USER object scopes/class layers/Events against supply
local roots to reject LOCAL leakage into portable USER.

Actual `adb install -r` replaced padding12 APK/supply with a distinct compatible
padding24 revision at stable definition IDs and the same S0 schema. Untouched
policy changed12 ->24. Native Add bounds changed `[31,1739][1049,1865]` ->
`[63,1707][1017,1833]`. A separate model Event override31 remained31, with native
bounds unchanged at `[81,1689][999,1815]`. Portable semantics and own LOCAL
preferences/revision survived. The acceptance-only option is
`-Papptraverse.testSupplyPadding=24`; ordinary builds omit it.

## Executed OS and required-binding checks

Live480dpi/fontScale1.3/night-on reached LOCAL revision33. An unchanged cold restart
retained33. Stopped OS settings then changed to500dpi/fontScale1.5/night-off. Startup
and initial native configuration settling produced revision36 with the real final
values. A second unchanged cold restart retained36. Prepare performs Startup
comparison before publication; subsequent real native notifications can replace
last_reason with SystemNotification. The last reason is not treated as a record
of the whole startup sequence.

The harness waits for actual onStop LOCAL-file modification before reading or
killing. Earlier diagnostic runs read an existing file too early and were not
accepted as persistence evidence. Temporary native trace was removed and the final
APKs were rebuilt; the final Debug/load-only/MinimalApp libraries were checked for
its absence.

The target fixture's `PROFILE missing-control` mode commits a valid SetOverride
Event naming control424242. Actual Activity launch aborted at completed dispatch
with `fatal: Android layout requires an unmounted native control`, SIGABRT and
failed-process exit. This checks required composition, not corrupt-storage or
production-recovery scenarios.

## Independent consumer and fresh policy

MinimalApp Android uses the same generic USER+LOCAL session, startup snapshot,
shared native measurement/solver and LOCAL observations. Its shell applies active
page visibility locally and submits no native viewport pixels to portable layout.
Its host exporter target/output were shortened after the actual deep-path MSBuild
file-tracker failure in this workspace.

Actual MinimalApp emulator smoke passed Apply -> Applied/disabled, Open Detail,
Close Detail, no working-action disk save, native USER8716bytes/LOCAL1186bytes at
onStop, and cold load-only restart retaining Applied/disabled.

Normal Debug's narrow native cold-restart test passed **OK(1test),23.833seconds**:
the second process began with a fresh empty graph, prior USER bytes stayed intact,
and the native LOCAL pair was written at lifecycle. Full normal-Debug
instrumentation is not claimed.

Raw evidence is in task `outputs/android-acceptance`: `os-final.log`,
`settings-evidence.json`, `supply-update.log`, `missing-binding.log`,
`minimal-smoke.log`, `instrument-debug-fresh.log`, and recorded APK/fixture outputs.
Earlier failed harness logs are retained separately; they are not PASS evidence.

## Native UI regression status at this handoff

**Seven distinct cases verified; two Recents cases remain FAIL.** This is not a
9/9 suite PASS and does not replace the historical Android acceptance scope.

The full current load-only invocation returned `Tests run:9, Failures:3` in
`instrument-loadonly-final2.log`. Its six successful cases are empty-ring swipe,
Add/navigation/Close/reopen/explicit Open/Delete, native background cold restore,
RU/EN propagation, rotation/Activity remount, and live native SP enlargement.
The narrowed three-case invocation `instrument-narrow-final2.log` returned
`Tests run:3, Failures:2`; its successful appearance case verifies all native
Auto/Light/Dark/soft menu states, RAM-until-lifecycle behavior and restored LOCAL
preferences. The task selector was corrected to the observed API34
`topResumedActivity=` field, and native menu state is read from the closest row
ancestor containing exactly one native checkable control.

The remaining cases are Home/Recents-dismiss and Back/Recents-dismiss followed by
cold restore. The current foreground task ID is captured before backgrounding;
only older own-app task IDs are removed as a test precondition. The current task
is never removed through ActivityManager. It must disappear from actual anchored
`* Recent #... Task{...}` entries after native UI dismissal. The final bounded
native accessibility probe exposed no ACTION_DISMISS/Close app action on this
card, so it used the native gesture and returned `Tests run:2, Failures:2`,
87.081seconds, in `instrument-recents-native-action.log`.

A separate external-adb timed swipe also left the current task in actual
RecentTasks with `inRecents=true`; this was not a savedTaskIds/history match.
Native Recents before/after screenshots and hierarchy were recorded; the card
became visually blank but its task entry remained. onStop USER/LOCAL files had
completed (6531/1186bytes, timestamps00:58:57 and00:59:38 in
`recents-lifecycle-files.txt`). Home's failure snapshot still held a stopped
Activity; the later Back/external record had Activities empty. No spontaneous
reopen was observed. The precise cause between input, launcher and application
handoff remains unresolved; it is not asserted to be an OS-only limitation.

Evidence: `instrument-recents-final.log`, `instrument-recents-native-action.log`,
`external-recents-command.log`, `external-recents-probe.log`,
`final-recents-before.png`, `final-recents-after.png`, `recents-final-state.xml`.
These two failures remain explicit despite the separate successful USER/LOCAL
transfers, compatible APK/supply update and OS comparison checks above.

## Independent follow-up and final palette correction

An independent GPT-6.1 Sol high worker reproduced the same current-task dismissal
failure on stock Android Settings (task911), with the same native card geometry,
several explicit-display/center gestures, and native Delete keys. A tap on that
card reopened Settings. Lock-task mode was NONE; accessibility/input filtering
were off. Temporarily enabling all animation scales and restarting only Pixel
Launcher did not fix dismissal; original scales0 were restored. No Catalog or
stock current task was removed through task APIs. This reproduces the failure
outside the application and supports an emulator launcher/input boundary finding,
while task dismissal remains NOT PASS. Evidence:
`sol-independent-recents-diagnosis.md` and its `sol-recents-*` artifacts in the raw
output directory. Application onStop persistence/cold-load checks remain valid at
the separately executed boundaries above.

The final accessibility closeout preserves the native disabled foreground color
and alpha when soft dark mode replaces the enabled foreground. Native button
background/state feedback is retained. Final base-APK narrow regression returned
**OK(2tests),52.929seconds** in `instrument-appearance-final.log`: all existing
appearance/LOCAL cold-restore assertions plus a real Android Button comparison
against Theme.Material's disabled text state. This adds one native palette check;
it does not turn the two Recents failures into successes. No full ten-case suite
PASS is claimed.

Default and padding24 test supplies now use separate exporter CMake caches, so a
test revision cannot leave its defaults selected for an ordinary subsequent build.
The private exporter target is shortened for actual Windows MAX_PATH object-file
limits; normal output binary naming is retained. Final Debug, load-only, test
variant and MinimalApp APK hashes are recorded in the artifact manifest below.

| Final APK | SHA256 |
| --- | --- |
| window-catalog-load-only-default12 | `a3abe0af5c5107219c5470ce69dd3baed88388e81e32fb352e61394e5ca0fa99` |
| window-catalog-load-only-padding24 | `5fcf50720b93a141ed9b7886ed8a894fe5dcb0c451d09a68631c54b9c3186064` |
| window-catalog-debug-fresh | `947ae94d930de3946ce5a33af96ee1ea27bcb96466136c2a04f4814639de9f0c` |
| minimal-app-load-only | `3e497d467d0b5f4bde9124805ae40a352d2af0a30a1cd734c3e745822212c898` |

Final packaging logs: `build-loadonly-final.log` (16s), `build-padding24-final.log` (12s), `build-debug-final.log` (11s), `build-minimal-final.log` (8s).
Only the final manifest artifacts above are labeled current; earlier raw diagnostic APKs are retained as historical evidence.
