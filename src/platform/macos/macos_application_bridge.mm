#import "apptraverse/platform/macos/ATMacApplicationBridge.h"
#import <AppKit/AppKit.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <memory>
#include <map>
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

// SwiftUI's full-size content includes the title bar in contentRect. The
// model's semantic size is the unobscured layout viewport.
NSSize ContentSizeForViewport(NSWindow* window, double width, double height) {
  auto const current = [window contentRectForFrameRect:window.frame].size;
  auto const viewport = window.contentLayoutRect.size;
  return NSMakeSize(width + current.width - viewport.width,
                    height + current.height - viewport.height);
}

struct NativeWindow {
  __weak NSWindow* window{};
  std::uint64_t placement_revision{};
  bool applying_frame{};
  bool own_initial_placement{};
};

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
  if ((self = [super init])) { _title = title; _selected = selected; }
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

@interface ATMacLayoutResult ()
- (instancetype)initWithResult:(apptraverse::ui::UiConstraintLayoutResult const&)result;
@end

@implementation ATMacLayoutResult
- (instancetype)initWithResult:(apptraverse::ui::UiConstraintLayoutResult const&)result {
  if ((self = [super init])) {
    _contentSize = CGSizeMake(result.content_width, result.content_height);
    NSMutableDictionary<NSNumber*, NSValue*>* frames = [NSMutableDictionary dictionary];
    for (auto const& frame : result.controls) {
      frames[@(frame.control_id)] = [NSValue valueWithRect:
          CGRectMake(frame.x, frame.y, frame.width, frame.height)];
    }
    _frames = [frames copy];
  }
  return self;
}
@end

@interface ATMacWindowLayout () {
  apptraverse::ui::UiLayoutNode _root;
}
- (instancetype)initWithRoot:(apptraverse::ui::UiLayoutNode const&)root;
@end

@implementation ATMacWindowLayout
- (instancetype)initWithRoot:(apptraverse::ui::UiLayoutNode const&)root {
  if ((self = [super init])) _root = root;
  return self;
}

- (ATMacLayoutResult*)solveViewport:(CGSize)viewport
                        controlIDs:(NSArray<NSNumber*>*)controlIDs
                           measure:(CGSize (^)(uint32_t, double))measure {
  using namespace apptraverse::ui;
  std::set<std::uint32_t> slots;
  auto collect = [&](auto&& self, UiLayoutNode const& node) -> void {
    if (node.flow == UiLayoutFlow::kLeaf) slots.insert(node.control_id);
    for (auto const& child : node.children) self(self, child);
  };
  collect(collect, _root);
  std::vector<UiLayoutMeasurement> measurements;
  for (NSNumber* control in controlIDs) {
    auto const id = control.unsignedIntValue;
    if (slots.erase(id) != 1) {
      apptraverse::WriteFatalStderr("duplicate or unknown macOS layout control\n");
      std::abort();
    }
    auto const ideal = measure(id, -1);
    auto const minimum = measure(id, 0);
    measurements.push_back({id, minimum.width, ideal.height,
                            ideal.width, ideal.height});
  }
  if (!slots.empty()) {
    std::string detail = "missing required macOS layout controls:";
    for (auto const id : slots) detail += " " + std::to_string(id);
    detail += "; mounted:";
    for (NSNumber* id in controlIDs) detail += " " + std::to_string(id.unsignedIntValue);
    detail += "\n";
    apptraverse::WriteFatalStderr(detail.c_str());
    std::abort();
  }
  auto result = ComputeUiConstraintLayout(_root, viewport.width, viewport.height,
      measurements, [&](std::uint32_t id, double width) {
        auto measured = *std::find_if(measurements.begin(), measurements.end(),
            [&](auto const& entry) { return entry.control_id == id; });
        auto const size = measure(id, width);
        measured.min_height = std::max(measured.min_height, size.height);
        measured.preferred_height = std::max(measured.preferred_height, size.height);
        return measured;
      });
  if (!result.valid) {
    apptraverse::WriteFatalStderr(result.error.c_str());
    std::abort();
  }
  return [[ATMacLayoutResult alloc] initWithResult:result];
}
@end

@interface ATMacWindowState ()
- (instancetype)initWithTitle:(NSString*)title
                        width:(double)width
                       height:(double)height
                      primary:(BOOL)primary
                       layout:(ATMacWindowLayout*)layout
                       theme:(NSUInteger)theme softenDark:(BOOL)softenDark;
@end

@implementation ATMacWindowState
- (instancetype)initWithTitle:(NSString*)title
                        width:(double)width
                       height:(double)height
                      primary:(BOOL)primary
                       layout:(ATMacWindowLayout*)layout
                       theme:(NSUInteger)theme softenDark:(BOOL)softenDark {
  if ((self = [super init])) {
    _title = title;
    _width = width;
    _height = height;
    _primary = primary;
    _layout = layout;
    _theme = theme;
    _softenDark = softenDark;
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
  for (auto& [token, native] : _windows) {
    [self applyLocalPlacement:token];
  }
  [self.delegate bridgeDidFinishPublication:self];
  _runtime->NotifyPublicationConsumed();
}

- (ATMacWindowState*)windowState:(uint64_t)token {
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  auto desktop = presenter.desktop_window();
  auto const appearance = presenter.LocalDeviceState()->appearance;
  NSString* title = ToNSString(presenter.ResolvePresentedWindowTitle(
      presenter.locale->language_index()));
  return [[ATMacWindowState alloc]
      initWithTitle:title
              width:desktop->restore_client_width()
             height:desktop->restore_client_height()
            primary:desktop->obj_id == _mainWindowId
             layout:[[ATMacWindowLayout alloc] initWithRoot:*presenter.LayoutRoot()]
              theme:static_cast<NSUInteger>(appearance.theme)
         softenDark:appearance.soften_dark];
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

- (void)bindNativeWindow:(id)window token:(uint64_t)token {
  auto& native = _windows[token];
  native.window = static_cast<NSWindow*>(window);
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  if ([self applyLocalPlacement:token]) return;

  auto const desktop = presenter.desktop_window();
  native.applying_frame = true;
  if (desktop->HasExplicitPosition()) {
    [self placeSemanticFrame:token];
  } else {
    [native.window setContentSize:ContentSizeForViewport(native.window,
        desktop->restore_client_width(), desktop->restore_client_height())];
  }
  native.applying_frame = false;
  [self observeNativeWindow:token commit:NO];
}

- (void)unbindNativeWindow:(uint64_t)token { _windows.erase(token); }

- (void)observeNativeWindow:(uint64_t)token commit:(BOOL)commit {
  auto const found = _windows.find(token);
  if (_stopping || found == _windows.end() || found->second.applying_frame) return;
  auto& native = found->second;
  NSWindow* window = native.window;
  if (!window || window.isMiniaturized || window.isZoomed ||
      (window.styleMask & NSWindowStyleMaskFullScreen) || !window.screen) return;
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  auto* state = presenter.LocalDeviceState();
  auto const frame = window.frame;
  auto const area = window.screen.visibleFrame;
  UiDisplayRect const outer{ModelCoordinate(frame.origin.x - area.origin.x),
      ModelCoordinate(NSMaxY(area) - NSMaxY(frame)),
      ModelCoordinate(frame.size.width), ModelCoordinate(frame.size.height)};
  if (!state->PlacementFor(presenter.window.id())) native.own_initial_placement = true;
  auto const dpi = static_cast<std::uint32_t>(std::lround(96 * window.backingScaleFactor));
  _proxy->Invoke(state->obj_id, &UiDeviceState::ObserveWindow,
      UiWindowEnvironment{presenter.window.id(), ScreenKey(window.screen), dpi, dpi, outer},
      commit ? UiEnvironmentReason::kUserWindowChange : UiEnvironmentReason::kSystemNotification);
}

- (BOOL)applyLocalPlacement:(uint64_t)token {
  auto& native = _windows.at(token);
  NSWindow* window = native.window;
  auto& presenter = FromToken<MacDesktopWindowPresenter>(token);
  auto const* placement = presenter.LocalDeviceState()->PlacementFor(presenter.window.id());
  if (!window || !placement || placement->revision == native.placement_revision ||
      placement->resolution == UiMonitorResolution::kUnavailable) return NO;
  if (native.own_initial_placement && placement->revision == 1) {
    native.placement_revision = placement->revision;
    return NO;
  }
  for (NSScreen* screen in NSScreen.screens) {
    if (ScreenKey(screen) != placement->resolved_monitor) continue;
    auto const area = screen.visibleFrame;
    auto const saved = placement->frame_dip;
    double const width = std::clamp<double>(saved.width, 1, std::max(1.0, area.size.width));
    double const height = std::clamp<double>(saved.height, 1, std::max(1.0, area.size.height));
    auto const target = NSMakeRect(area.origin.x + std::clamp<double>(saved.x, 0, area.size.width - width),
        NSMaxY(area) - std::clamp<double>(saved.y, 0, area.size.height - height) - height, width, height);
    native.placement_revision = placement->revision;
    native.applying_frame = true;
    [window setFrame:target display:YES];
    native.applying_frame = false;
    [self observeNativeWindow:token commit:NO];
    return YES;
  }
  return NO;
}

- (void)placeSemanticFrame:(uint64_t)token {
  NSWindow* window = _windows.at(token).window;
  auto const desktop = FromToken<MacDesktopWindowPresenter>(token).desktop_window();
  auto const size = ContentSizeForViewport(window, desktop->restore_client_width(),
                                            desktop->restore_client_height());
  auto const outer = [window frameRectForContentRect:NSMakeRect(0, 0, size.width, size.height)];
  auto const primary = NSScreen.screens.firstObject;
  [window setFrame:NSMakeRect(desktop->x(), NSMaxY(primary.frame) - desktop->y() - outer.size.height,
                            outer.size.width, outer.size.height) display:YES];
}

- (void)applyRequestedNativeFrame:(uint64_t)token {
  auto const found = _windows.find(token);
  if (found == _windows.end() || !found->second.window) return;
  found->second.applying_frame = true;
  [self placeSemanticFrame:token];
  found->second.applying_frame = false;
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
