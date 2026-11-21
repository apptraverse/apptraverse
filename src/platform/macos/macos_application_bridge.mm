#import "apptraverse/platform/macos/ATMacApplicationBridge.h"
#import <AppKit/AppKit.h>
#import "apptraverse/platform/apple/apple_window_layout.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/macos/macos_application_bridge.h"
#include "apptraverse/platform/macos/macos_ui_native_views.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

using apptraverse::ui::UiDisplayRect;
using apptraverse::ui::UiDeviceState;
using apptraverse::ui::UiEnvironmentReason;
using apptraverse::ui::UiMonitorResolution;
using apptraverse::ui::UiWindowEnvironment;
using apptraverse::ui::macos::MacDesktopWindowPresenter;
using apptraverse::ui::macos::MacListContainerPresenter;
using apptraverse::ui::macos::MacPushButtonPresenter;

namespace {

std::int32_t ModelCoordinate(double value) {
  if (!std::isfinite(value) ||
      value < std::numeric_limits<std::int32_t>::min() ||
      value > std::numeric_limits<std::int32_t>::max()) {
    apptraverse::WriteFatalStderr("invalid native macOS coordinate\n");
    std::abort();
  }
  return static_cast<std::int32_t>(value);
}

template <typename T>
uint64_t Token(T const& value) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&value));
}

template <typename T>
T& FromToken(uint64_t token) {
  return *reinterpret_cast<T*>(static_cast<uintptr_t>(token));
}

NSString* ToNSString(std::string const& text) {
  NSString* result = [[NSString alloc] initWithBytes:text.data()
                                              length:text.size()
                                            encoding:NSUTF8StringEncoding];
  if (result == nil) {
    apptraverse::WriteFatalStderr("cannot create native text from UTF-8 publication\n");
    std::abort();
  }
  return result;
}

std::string ScreenKey(NSScreen* screen) {
  auto const display = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
  auto uuid = CGDisplayCreateUUIDFromDisplayID(display);
  if (!uuid) return "cgdisplay:" + std::to_string(display);
  NSString* key = CFBridgingRelease(CFUUIDCreateString(nullptr, uuid));
  CFRelease(uuid);
  return std::string{key.UTF8String};
}

NSScreen* ScreenForKey(std::string const& key) {
  for (NSScreen* screen in NSScreen.screens) {
    if (ScreenKey(screen) == key) return screen;
  }
  return nil;
}

NSRect FrameInWorkArea(NSRect area, UiDisplayRect saved) {
  double const width = std::clamp<double>(saved.width, 1, std::max(1.0, area.size.width));
  double const height = std::clamp<double>(saved.height, 1, std::max(1.0, area.size.height));
  return NSMakeRect(area.origin.x + std::clamp<double>(saved.x, 0, area.size.width - width),
      NSMaxY(area) - std::clamp<double>(saved.y, 0, area.size.height - height) - height,
      width, height);
}

// SwiftUI's full-size content includes the title bar in contentRect. The
// model's semantic size excludes both that title bar and the solver-measured
// SwiftUI window chrome above the application layout.
NSSize ContentSizeForViewport(NSWindow* window, double width, double height,
                              double chrome_height) {
  auto const current = [window contentRectForFrameRect:window.frame].size;
  auto const viewport = window.contentLayoutRect.size;
  return NSMakeSize(width + current.width - viewport.width,
                    height + chrome_height + current.height - viewport.height);
}

struct NativeWindow {
  __weak NSWindow* window{};
  std::uint64_t placement_revision{};
  bool applying_frame{};
  bool moving{};
  bool own_initial_placement{};
  bool pending_requested_frame{};
  double chrome_height{};
  std::optional<double> applied_chrome_height;
  std::optional<bool> fullscreen_transition;
  std::optional<bool> awaiting_native_intent;
};

bool InFullscreen(NativeWindow const& native) {
  return native.fullscreen_transition.has_value() ||
      (native.window.styleMask & NSWindowStyleMaskFullScreen);
}

class MacNativeViews final
    : public apptraverse::ui::macos::MacUiNativeViews {
 public:
  explicit MacNativeViews(ATMacApplicationBridge* owner) : owner_{owner} {}

  void LoadDesktopWindow(MacDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadWindow:Token(presenter)];
  }
  void SyncDesktopWindow(MacDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncWindow:Token(presenter)];
  }
  void UnloadDesktopWindow(MacDesktopWindowPresenter& presenter) override {
    [owner_ unbindNativeWindow:Token(presenter)];
    [owner_.delegate bridge:owner_ unloadWindow:Token(presenter)];
  }
  void ApplyRequestedDesktopFrame(MacDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ applyWindowFrame:Token(presenter)];
  }
  void OrderDesktopWindowFront(MacDesktopWindowPresenter& presenter,
                              bool activate) override {
    [owner_.delegate bridge:owner_ orderWindowFront:Token(presenter)
                   activate:activate];
  }
  std::vector<MacDesktopWindowPresenter*> DesktopZOrder() override {
    std::vector<MacDesktopWindowPresenter*> result;
    for (NSNumber* token in [owner_.delegate orderedWindowTokens]) {
      result.push_back(&FromToken<MacDesktopWindowPresenter>(token.unsignedLongLongValue));
    }
    return result;
  }
  void LoadPushButton(MacPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadButton:Token(presenter)];
  }
  void SyncPushButton(MacPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncButton:Token(presenter)];
  }
  void UnloadPushButton(MacPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadButton:Token(presenter)];
  }
  void LoadListContainer(MacListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadList:Token(presenter)];
  }
  void SyncListContainer(MacListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncList:Token(presenter)];
  }
  void UnloadListContainer(MacListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadList:Token(presenter)];
  }

 private:
  __unsafe_unretained ATMacApplicationBridge* owner_;
};

}  // namespace

@implementation ATMacMenuItemState
- (instancetype)initWithTitle:(NSString*)title selected:(BOOL)selected {
  return [self initWithTitle:title selected:selected startsSection:NO];
}
- (instancetype)initWithTitle:(NSString*)title selected:(BOOL)selected
                startsSection:(BOOL)startsSection {
  if ((self = [super init])) {
    _title = title;
    _selected = selected;
    _startsSection = startsSection;
  }
  return self;
}
@end

@implementation ATMacMenuGroupState
- (instancetype)initWithTitle:(NSString*)title
                        items:(NSArray<ATMacMenuItemState*>*)items {
  if ((self = [super init])) { _title = title; _items = [items copy]; }
  return self;
}
@end

@implementation ATMacWindowDisplayState
- (instancetype)initWithFullscreen:(BOOL)fullscreen actionTitle:(NSString*)actionTitle
                    toolbarLayout:(ATAppleWindowLayout*)toolbarLayout
                   actionControlID:(uint32_t)actionControlID {
  if ((self = [super init])) {
    _fullscreen = fullscreen;
    _actionTitle = actionTitle;
    _toolbarLayout = toolbarLayout;
    _actionControlID = actionControlID;
  }
  return self;
}
@end

@interface ATMacWindowState ()
- (instancetype)initWithTitle:(NSString*)title
                        width:(double)width
                       height:(double)height
                      primary:(BOOL)primary
                       layout:(ATAppleWindowLayout*)layout
                       theme:(NSUInteger)theme softenDark:(BOOL)softenDark
                     display:(ATMacWindowDisplayState*)display;
@end

@implementation ATMacWindowState
- (instancetype)initWithTitle:(NSString*)title
                        width:(double)width
                       height:(double)height
                      primary:(BOOL)primary
                       layout:(ATAppleWindowLayout*)layout
                       theme:(NSUInteger)theme softenDark:(BOOL)softenDark
                     display:(ATMacWindowDisplayState*)display {
  if ((self = [super init])) {
    _title = title;
    _width = width;
    _height = height;
    _primary = primary;
    _layout = layout;
    _theme = theme;
    _softenDark = softenDark;
    _display = display;
  }
  return self;
}
@end

@interface ATMacButtonState ()
- (instancetype)initWithParentToken:(uint64_t)parentToken
                              title:(NSString*)title
                            enabled:(BOOL)enabled
                     controlID:(uint32_t)controlID;
@end

@implementation ATMacButtonState
- (instancetype)initWithParentToken:(uint64_t)parentToken
                              title:(NSString*)title
                            enabled:(BOOL)enabled
                     controlID:(uint32_t)controlID {
  if ((self = [super init])) {
    _parentToken = parentToken;
    _title = title;
    _enabled = enabled;
    _controlID = controlID;
  }
  return self;
}
@end

@interface ATMacListRowState ()
- (instancetype)initWithIdentity:(uint64_t)identity text:(NSString*)text;
@end

@implementation ATMacListRowState
- (instancetype)initWithIdentity:(uint64_t)identity text:(NSString*)text {
  if ((self = [super init])) {
    _identity = identity;
    _text = text;
  }
  return self;
}
@end

@interface ATMacListState ()
- (instancetype)initWithParentToken:(uint64_t)parentToken
                               rows:(NSArray<ATMacListRowState*>*)rows
                        selectedRow:(NSInteger)selectedRow
                     controlID:(uint32_t)controlID;
@end

@implementation ATMacListState
- (instancetype)initWithParentToken:(uint64_t)parentToken
                               rows:(NSArray<ATMacListRowState*>*)rows
                        selectedRow:(NSInteger)selectedRow
                     controlID:(uint32_t)controlID {
  if ((self = [super init])) {
    _parentToken = parentToken;
    _rows = rows;
    _selectedRow = selectedRow;
    _controlID = controlID;
  }
  return self;
}
@end

@interface ATMacApplicationBridge () {
  apptraverse::ui::macos::MacApplicationBridgeCallbacks _callbacks;
  apptraverse::ApplicationModelRuntime _ownedRuntime;
  apptraverse::ApplicationModelRuntime* _runtime;
  std::unique_ptr<apptraverse::ModelObjectProxy> _proxy;
  std::unique_ptr<MacNativeViews> _nativeViews;
  std::unique_ptr<apptraverse::ApplicationUiSession> _uiSession;
  std::thread _modelThread;
  int _modelExitStatus;
  bool _stopping;
  ae::ObjId _mainWindowId;
  std::map<uint64_t, NativeWindow> _windows;
}
@property(nonatomic, weak, readwrite)
    id<ATMacApplicationBridgeDelegate> delegate;
- (instancetype)initWithCallbacks:
                    (apptraverse::ui::macos::MacApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATMacApplicationBridgeDelegate>)delegate;
@end

@implementation ATMacApplicationBridge

- (instancetype)initWithCallbacks:
                    (apptraverse::ui::macos::MacApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATMacApplicationBridgeDelegate>)delegate {
  if ((self = [super init])) {
    _callbacks = std::move(callbacks);
    _runtime = _callbacks.runtime ? _callbacks.runtime : &_ownedRuntime;
    _delegate = delegate;
    _modelExitStatus = 1;
    _stopping = false;
  }
  return self;
}

- (void)start {
  _proxy = std::make_unique<apptraverse::ModelObjectProxy>(
      [self](apptraverse::ModelObjectProxy::ModelWork work) {
        _runtime->Post(std::move(work));
      });
  _nativeViews = std::make_unique<MacNativeViews>(self);
  _modelThread = std::thread([self] {
    _modelExitStatus = _callbacks.run_model(
        *_runtime, [self](apptraverse::ApplicationPublicationKind kind) {
          dispatch_async(dispatch_get_main_queue(), ^{
            [self
                consumePublication:
                    kind == apptraverse::ApplicationPublicationKind::Initial
                        ? ATMacPublicationKindInitial
                        : ATMacPublicationKindIncremental];
          });
        });
    dispatch_async(dispatch_get_main_queue(), ^{
      id<ATMacApplicationBridgeDelegate> delegate = self.delegate;
      if (delegate != nil) {
        [delegate bridgeModelDidFinish:self];
      } else {
        // The Swift host can be released before the model finishes. Break the
        // model/proxy lifetime only after the worker has actually returned.
        [self finishTeardown];
      }
    });
  });
}

- (void)consumePublication:(ATMacPublicationKind)kind {
  if (self.delegate == nil) {
    // Host destruction is a teardown state, not a partially loaded UI. Drain
    // the publication slot so the stopped model can finish without a host.
    [self requestStop];
    _runtime->AbandonUnconsumedPublicationsForTeardown();
    return;
  }
  std::vector<std::uint8_t> publication =
      _runtime->TakePublishedCopyForUiApply(
          kind == ATMacPublicationKindInitial
              ? apptraverse::TakePublishedSidecarCallback{}
              : _callbacks.take_incremental_sidecar);
  if (kind == ATMacPublicationKindInitial) {
    _uiSession = std::make_unique<apptraverse::ApplicationUiSession>(
        publication, _nativeViews.get(), *_proxy, [self](ae::Obj& root) {
          _mainWindowId = _callbacks.main_window_id(root);
          if (_callbacks.prepare_ui_presenters) {
            _callbacks.prepare_ui_presenters(root);
          }
        });
    if (_callbacks.after_initial) _callbacks.after_initial(_uiSession->root());
  } else {
    _uiSession->ApplyStructural(publication);
    if (_callbacks.after_incremental) _callbacks.after_incremental(_uiSession->root());
  }
  if (_callbacks.menu_state) {
    [self.delegate bridge:self syncMenu:_callbacks.menu_state(_uiSession->root())];
  }
  if (_callbacks.context_menu_state) {
    [self.delegate bridge:self syncContextMenu:_callbacks.context_menu_state(_uiSession->root())];
  }
  for (auto& [token, native] : _windows) {
    [self applyLocalPlacement:token];
  }
  [self.delegate bridgeDidFinishPublication:self];
  _runtime->NotifyPublicationConsumed();
}

- (ATMacWindowState*)windowState:(uint64_t)token {
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  auto desktop = presenter.desktop_window();
  auto const& device = *presenter.LocalDeviceState();
  auto const appearance = device.appearance;
  int const language = presenter.locale->language_index();
  NSString* title = ToNSString(presenter.ResolvePresentedWindowTitle(language));
  auto* display = _callbacks.window_display_state
      ? _callbacks.window_display_state(device, desktop->obj_id, language) : nil;
  return [[ATMacWindowState alloc]
      initWithTitle:title
              width:desktop->restore_client_width()
             height:desktop->restore_client_height()
            primary:desktop->obj_id == _mainWindowId
             layout:[[ATAppleWindowLayout alloc] initWithRoot:*presenter.LayoutRoot()]
              theme:static_cast<NSUInteger>(appearance.theme)
         softenDark:appearance.soften_dark
            display:display];
}

- (ATMacButtonState*)buttonState:(uint64_t)token {
  auto& presenter = FromToken<MacPushButtonPresenter>(token);
  NSString* title = ToNSString(presenter.button->label->Resolve(
      presenter.window_presenter->locale->language_index()));
  return [[ATMacButtonState alloc]
      initWithParentToken:Token(*presenter.window_presenter)
                    title:title
                  enabled:presenter.button->enabled()
                 controlID:presenter.control_id];
}

- (ATMacListState*)listState:(uint64_t)token {
  auto& presenter = FromToken<MacListContainerPresenter>(token);
  int const language_index =
      presenter.window_presenter->locale->language_index();
  NSMutableArray<ATMacListRowState*>* rows =
      [[NSMutableArray alloc] initWithCapacity:presenter.container->rows.size()];
  for (auto const& row : presenter.container->rows) {
    auto const identity = static_cast<std::uint64_t>(row.id().id());
    [rows addObject:[[ATMacListRowState alloc]
                        initWithIdentity:identity
                                   text:ToNSString(presenter.ResolveListRowPrimaryText(
                                            row, language_index))]];
  }
  return [[ATMacListState alloc]
      initWithParentToken:Token(*presenter.window_presenter)
                     rows:rows
              selectedRow:presenter.container->selection_index
                 controlID:presenter.control_id];
}

- (void)executeMenuGroup:(NSUInteger)group item:(NSUInteger)item {
  if (_stopping || !_uiSession || !_callbacks.on_menu_command) return;
  _callbacks.on_menu_command(_uiSession->root(), *_proxy,
      static_cast<unsigned>(group), static_cast<unsigned>(item));
}

- (void)executeContextMenuItem:(NSUInteger)item {
  if (_stopping || !_uiSession || !_callbacks.on_context_menu_command) return;
  _callbacks.on_context_menu_command(_uiSession->root(), *_proxy,
      static_cast<unsigned>(item));
}

- (void)submitWindowFullscreen:(BOOL)fullscreen token:(uint64_t)token {
  if (_stopping || !_callbacks.on_window_fullscreen || !_windows.contains(token)) return;
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  _callbacks.on_window_fullscreen(*_proxy, presenter.LocalDeviceState()->obj_id,
      presenter.window.id(), fullscreen);
}

- (void)clickButton:(uint64_t)token {
  if (_stopping) return;
  FromToken<MacPushButtonPresenter>(token).NotifyClickedFromUi();
}

- (void)selectListRow:(NSInteger)row token:(uint64_t)token {
  if (_stopping) return;
  FromToken<MacListContainerPresenter>(token)
      .SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(row));
}

- (void)activateListRow:(NSInteger)row token:(uint64_t)token {
  if (_stopping) return;
  FromToken<MacListContainerPresenter>(token)
      .SubmitListRowActivatedFromUi(static_cast<std::size_t>(row));
}

- (void)bindNativeWindow:(id)window token:(uint64_t)token
           chromeHeight:(double)chromeHeight {
  auto& native = _windows[token];
  native.window = static_cast<NSWindow*>(window);
  native.chrome_height = chromeHeight;
  native.applied_chrome_height = chromeHeight;
  if (_callbacks.window_display_state) {
    native.window.collectionBehavior = (native.window.collectionBehavior &
        ~(NSWindowCollectionBehaviorFullScreenNone | NSWindowCollectionBehaviorFullScreenAuxiliary))
        | NSWindowCollectionBehaviorFullScreenPrimary;
  }
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  if ([self applyLocalPlacement:token]) {
    // LOCAL supplies an already complete normal outer frame. The initial
    // scene can measure a different width or the fullscreen exit action;
    // establish the normal baseline at the restored width without resizing it.
    native.applied_chrome_height.reset();
    return;
  }

  auto const desktop = presenter.desktop_window();
  native.applying_frame = true;
  if (desktop->HasExplicitPosition()) {
    [self placeSemanticFrame:token];
  } else {
    [native.window setContentSize:ContentSizeForViewport(native.window,
        desktop->restore_client_width(), desktop->restore_client_height(), native.chrome_height)];
  }
  native.applying_frame = false;
  [self observeNativeWindow:token commit:NO];
}

- (void)unbindNativeWindow:(uint64_t)token { _windows.erase(token); }

- (void)observeWindowChromeHeight:(double)height width:(double)width token:(uint64_t)token
                      fullscreen:(BOOL)fullscreen {
  auto const found = _windows.find(token);
  if (_stopping || found == _windows.end()) return;
  auto& native = found->second;
  // Fullscreen uses the work area and retains its independent normal frame.
  // Late SwiftUI measurements from the other presentation must not resize it.
  if (fullscreen || InFullscreen(native) || !native.window ||
      std::abs(width - native.window.contentLayoutRect.size.width) > 0.5) return;
  native.chrome_height = height;
  [self reconcileWindowChrome:token];
}

- (void)reconcileWindowChrome:(uint64_t)token {
  auto& native = _windows.at(token);
  NSWindow* window = native.window;
  if (!window || native.applying_frame || InFullscreen(native) || native.moving ||
      window.inLiveResize || window.isMiniaturized || window.isZoomed) return;
  if (!native.applied_chrome_height) {
    native.applied_chrome_height = native.chrome_height;
    return;
  }
  double const delta = native.chrome_height - *native.applied_chrome_height;
  if (delta == 0) return;
  native.applied_chrome_height = native.chrome_height;
  // Preserve the current client size, including a user's native resize, when
  // localization or native control measurement changes the chrome height.
  auto frame = window.frame;
  frame.origin.y -= delta;
  frame.size.height += delta;
  native.applying_frame = true;
  [window setFrame:frame display:YES];
  native.applying_frame = false;
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  auto const* placement = presenter.LocalDeviceState()->PlacementFor(presenter.window.id());
  // Keep a completed semantic resize's LOCAL outer frame in step with its
  // delayed native measurement, without replacing a missing preferred monitor
  // with the automatic fallback display.
  BOOL const commit = window.screen &&
      (!placement || placement->preferred_monitor == ScreenKey(window.screen));
  [self observeNativeWindow:token commit:commit];
}

- (void)applyWindowFullscreen:(BOOL)fullscreen token:(uint64_t)token {
  auto const found = _windows.find(token);
  if (_stopping || found == _windows.end()) return;
  auto& native = found->second;
  if (native.awaiting_native_intent) {
    if (*native.awaiting_native_intent != static_cast<bool>(fullscreen)) return;
    native.awaiting_native_intent.reset();
  }
  NSWindow* window = native.window;
  if (!window || window.isMiniaturized || window.inLiveResize || native.moving ||
      std::any_of(_windows.begin(), _windows.end(), [](auto const& entry) {
        return entry.second.fullscreen_transition.has_value();
      }) || static_cast<bool>(fullscreen) == InFullscreen(native)) return;
  native.fullscreen_transition = static_cast<bool>(fullscreen);
  [window toggleFullScreen:nil];
}

- (void)nativeFullscreenWillChange:(BOOL)fullscreen token:(uint64_t)token {
  auto const found = _windows.find(token);
  if (_stopping || !_callbacks.on_window_fullscreen || found == _windows.end()) return;
  auto& native = found->second;
  if (native.fullscreen_transition == static_cast<bool>(fullscreen)) return;
  native.fullscreen_transition = static_cast<bool>(fullscreen);
  native.awaiting_native_intent = static_cast<bool>(fullscreen);
  [self submitWindowFullscreen:fullscreen token:token];
}

- (void)nativeFullscreenDidChange:(uint64_t)token {
  auto const found = _windows.find(token);
  if (found == _windows.end() || !_callbacks.on_window_fullscreen) return;
  auto& native = found->second;
  native.fullscreen_transition.reset();
  if (!InFullscreen(native)) {
    // AppKit restores its own pre-Space frame. Reapply the model's normal
    // placement against the current work area after the transition completes.
    native.placement_revision = 0;
    native.own_initial_placement = false;
  }
}

- (void)observeNativeWindow:(uint64_t)token commit:(BOOL)commit {
  auto const found = _windows.find(token);
  if (_stopping || found == _windows.end() || found->second.applying_frame) return;
  auto& native = found->second;
  NSWindow* window = native.window;
  if (!window || !window.screen) return;
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  auto* state = presenter.LocalDeviceState();
  // An explicit semantic frame has already been applied when commit is set.
  // AppKit can still report the old zoom state immediately after a Space exit.
  bool const normal = !InFullscreen(native) && !window.isMiniaturized &&
      (commit || !window.isZoomed);
  if (!state->PlacementFor(presenter.window.id())) {
    if (!normal) return;
    native.own_initial_placement = true;
  }
  _proxy->Invoke(state->obj_id, &UiDeviceState::ObserveWindow,
      apptraverse::ui::macos::ReadMacWindowEnvironment(window, presenter.window.id()),
      commit && normal ? UiEnvironmentReason::kUserWindowChange
                       : UiEnvironmentReason::kSystemNotification);
}

- (void)beginNativeWindowMove:(uint64_t)token {
  auto const found = _windows.find(token);
  if (!_stopping && found != _windows.end() && !found->second.applying_frame)
    found->second.moving = true;
}

- (void)finishNativeWindowInteraction:(uint64_t)token commit:(BOOL)commit {
  auto const found = _windows.find(token);
  if (_stopping || found == _windows.end() || found->second.applying_frame) return;
  auto& native = found->second;
  native.moving = false;
  [self reconcileWindowChrome:token];
  NSWindow* window = native.window;
  if (native.pending_requested_frame && !InFullscreen(native)) {
    [self applyRequestedNativeFrame:token];
    return;
  }
  if (commit && window && !window.isZoomed && !InFullscreen(native)) {
    // A completed user move takes priority over placement deferred during it.
    auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
    auto const* placement = presenter.LocalDeviceState()->PlacementFor(presenter.window.id());
    if (placement) native.placement_revision = placement->revision;
    [self observeNativeWindow:token commit:YES];
  } else if (![self applyLocalPlacement:token]) {
    [self observeNativeWindow:token commit:NO];
  }
}

- (BOOL)applyLocalPlacement:(uint64_t)token {
  auto& native = _windows.at(token);
  NSWindow* window = native.window;
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  auto const* placement = presenter.LocalDeviceState()->PlacementFor(presenter.window.id());
  if (!window || window.isMiniaturized || window.inLiveResize || native.moving || !placement ||
      InFullscreen(native) ||
      placement->revision == native.placement_revision ||
      placement->resolution == UiMonitorResolution::kUnavailable) return NO;
  if (native.own_initial_placement && placement->revision == 1) {
    native.placement_revision = placement->revision;
    return NO;
  }
  NSScreen* screen = ScreenForKey(placement->resolved_monitor);
  if (!screen) return NO;
  auto const area = screen.visibleFrame;
  // A newly mounted scene can inherit an AppKit zoomed rectangle from the
  // previous process. Apply LOCAL normal geometry before entering fullscreen.
  bool const preserve_zoom = native.placement_revision != 0 && window.isZoomed;
  auto const target = preserve_zoom
      ? area : FrameInWorkArea(area, placement->frame_dip);
  native.placement_revision = placement->revision;
  native.applying_frame = true;
  [window setFrame:target display:YES];
  native.applying_frame = false;
  [self observeNativeWindow:token commit:NO];
  return YES;
}

- (void)placeSemanticFrame:(uint64_t)token {
  auto& native = _windows.at(token);
  NSWindow* window = native.window;
  auto const desktop = FromToken<MacDesktopWindowPresenter>(token).desktop_window();
  auto const size = ContentSizeForViewport(window, desktop->restore_client_width(),
      desktop->restore_client_height(), native.chrome_height);
  native.applied_chrome_height = native.chrome_height;
  auto const outer = [window frameRectForContentRect:NSMakeRect(0, 0, size.width, size.height)];
  auto const primary = NSScreen.screens.firstObject;
  [window setFrame:NSMakeRect(desktop->x(), NSMaxY(primary.frame) - desktop->y() - outer.size.height,
                            outer.size.width, outer.size.height) display:YES];
}

- (void)applyRequestedNativeFrame:(uint64_t)token {
  auto const found = _windows.find(token);
  if (found == _windows.end() || !found->second.window) return;
  auto& native = found->second;
  if (InFullscreen(native) || native.moving || native.window.inLiveResize) {
    native.pending_requested_frame = true;
    return;
  }
  native.pending_requested_frame = false;
  native.applying_frame = true;
  [self placeSemanticFrame:token];
  native.applying_frame = false;
  [self observeNativeWindow:token commit:YES];
}

- (void)observeSystemEnvironment {
  if (_stopping || !_proxy) return;
  auto const environment = apptraverse::ui::macos::ReadMacDeviceEnvironment();
  std::set<ae::ObjId> roots;
  for (auto const& [token, native] : _windows) {
    auto* state = FromToken<MacDesktopWindowPresenter>(token).LocalDeviceState();
    if (roots.insert(state->obj_id).second) {
      _proxy->Invoke(state->obj_id, &UiDeviceState::ObserveEnvironment,
                     environment, UiEnvironmentReason::kSystemNotification);
    }
  }
}

- (void)activateWindow:(uint64_t)token {
  if (_stopping) return;
  FromToken<MacDesktopWindowPresenter>(token).NotifyActivatedFromUi();
}

- (BOOL)requestCloseWindow:(uint64_t)token {
  if (_stopping) return YES;
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  bool const stops_application = _callbacks.close_desktop_window(
      _uiSession->root(), *presenter.window, *_proxy,
      [self] { _runtime->RequestStop(); });
  _stopping = stops_application;
  return stops_application;
}

- (void)requestStop {
  if (_stopping) return;
  _stopping = true;
  if (self.delegate != nil && _callbacks.on_application_quit && _uiSession) {
    _callbacks.on_application_quit(_uiSession->root(), *_proxy,
                                  [self] { _runtime->RequestStop(); });
  } else {
    _runtime->RequestStop();
  }
}

- (NSInteger)finishTeardown {
  _modelThread.join();
  _uiSession.reset();
  _nativeViews.reset();
  _proxy.reset();
  self.delegate = nil;
  return _modelExitStatus;
}

@end

namespace apptraverse::ui::macos {

UiWindowEnvironment ReadMacWindowEnvironment(NSWindow* window, ae::ObjId id) {
  auto const frame = window.frame;
  auto const area = window.screen.visibleFrame;
  UiDisplayRect const outer{ModelCoordinate(frame.origin.x - area.origin.x),
      ModelCoordinate(NSMaxY(area) - NSMaxY(frame)),
      ModelCoordinate(frame.size.width), ModelCoordinate(frame.size.height)};
  auto const dpi = static_cast<std::uint32_t>(std::lround(96 * window.backingScaleFactor));
  return {id, ScreenKey(window.screen), dpi, dpi, outer};
}

bool ApplyMacWindowPlacement(NSWindow* window, UiWindowPlacement const& placement,
                             bool preserve_zoom) {
  NSScreen* screen = ScreenForKey(placement.resolved_monitor);
  if (!screen) return false;  // A display may disappear after publication.
  auto const area = screen.visibleFrame;
  [window setFrame:preserve_zoom ? area : FrameInWorkArea(area, placement.frame_dip)
           display:YES];
  return true;
}

UiDeviceEnvironment ReadMacDeviceEnvironment() {
  UiDeviceEnvironment environment;
  auto* workspace = NSWorkspace.sharedWorkspace;
  environment.supported_settings = kSystemDarkMode | kSystemHighContrast |
      kSystemReducedMotion | kSystemKeyboardCues | kSystemScreenReaderHint;
  environment.dark = [[NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:
      @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]] isEqualToString:NSAppearanceNameDarkAqua];
  environment.high_contrast = workspace.accessibilityDisplayShouldIncreaseContrast;
  environment.reduced_motion = workspace.accessibilityDisplayShouldReduceMotion;
  environment.keyboard_cues = NSApp.fullKeyboardAccessEnabled;
  environment.screen_reader_hint = workspace.voiceOverEnabled;
  NSScreen* primary = NSScreen.screens.firstObject;
  environment.system_dpi_x = environment.system_dpi_y =
      static_cast<unsigned>(std::lround(96 * primary.backingScaleFactor));
  for (NSScreen* screen in NSScreen.screens) {
    auto rect = [primary](NSRect value) -> UiDisplayRect {
      return {ModelCoordinate(value.origin.x), ModelCoordinate(NSMaxY(primary.frame) - NSMaxY(value)),
          ModelCoordinate(value.size.width), ModelCoordinate(value.size.height)};
    };
    auto const display = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    auto const refresh = mode ? static_cast<unsigned>(std::lround(CGDisplayModeGetRefreshRate(mode))) : 0u;
    if (mode) CGDisplayModeRelease(mode);
    environment.monitors.push_back({ScreenKey(screen), rect(screen.frame), rect(screen.visibleFrame),
        screen == primary, static_cast<unsigned>(CGDisplayRotation(display)), refresh});
  }
  NormalizeUiDeviceEnvironment(environment);
  return environment;
}

ATMacApplicationBridge* MakeMacApplicationBridge(
    MacApplicationBridgeCallbacks callbacks,
    id<ATMacApplicationBridgeDelegate> delegate) {
  return [[ATMacApplicationBridge alloc] initWithCallbacks:std::move(callbacks)
                                                 delegate:delegate];
}

}  // namespace apptraverse::ui::macos
