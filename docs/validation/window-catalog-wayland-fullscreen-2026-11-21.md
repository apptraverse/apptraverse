# WindowCatalog fullscreen geometry on WSLg Wayland

The MinSizeRel regression reproduced on WSLg 1.0.79 with GTK 3.24.41.
Wayland fullscreen requests and window configuration arrive separately.
One child restore sample already reported a GTK window of 410 × 328 while
the old viewport was still 428 × 268. Such samples could be classified as
user resizes and replace the saved normal placement.

The GTK presenter uses matching native and allocated client dimensions
for Wayland geometry observations. Completed window allocations schedule
the observation, including decoration changes that leave the scroller
size unchanged. Fullscreen requests are deduplicated until the requested
mode or placement changes. An early Escape waits for the fullscreen
allocation before sending the native exit.

Each entry takes a new normal client snapshot when local placement has
settled. The first mapped entry records the normal decoration extents
independently. When an explicit frame request was queued during fullscreen,
its accepted client size and those decoration extents determine the LOCAL
outer frame. GTK's animated shadow extents had made a child window restore
32 pixels too small in each dimension on cold launch.

The native three-process probe verifies normal viewports of 600 × 320 for
the main window and 410 × 280 for the child, with their saved LOCAL frames
preserved. It exercises live fullscreen exit, fullscreen exit after cold
load, and a cold restart after saving in windowed mode. All eight repeated
MinSizeRel runs passed on WSLg's native `wayland-0` socket with `DISPLAY`
unset. Each run used an isolated profile. Wayland's compositor owns
normal-window screen coordinates.

X11 under Xvfb/Openbox passed four MinSizeRel and three Debug CTests.
MinimalApp Debug passed its independent save and cold-restore processes.
The MinSizeRel headless model and Linux WindowCatalog binary contract
passed with the display environment unset.

The §25 read-only audit checked all 30 rules in the affected scope.

| Rules | Source disposition |
| --- | --- |
| 1–3, 7–9 | Graph composition, required resource checks, registration, and Debug producer entry points are outside the change. Added state belongs to the runtime presenter. |
| 4–6, 21 | Native mode and allocation acknowledgments are separate. The GTK presenter translates native bounds and sends accepted observations through the proxy. The existing model Events own placement. |
| 10–13, 15–18, 23 | The consumer loads the existing embedded Debug supply. USER/LOCAL use the existing RAM stores, S0 format, close/save boundaries, lazy graph, and history. |
| 14, 19, 20 | Presenter teardown disconnects signals and cancels the queued idle. The probe covers child Close/reopen and the translated native Exit action. |
| 22, 24–28 | The model remains shared with headless. GTK signals and a coalesced idle drive the native path. The scope adds no dependencies, runtime timers or persistent fields. |
| 29–30 | The executed scopes are itemized here. Publication uses the existing main history. |
