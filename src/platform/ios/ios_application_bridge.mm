#import "apptraverse/platform/ios/ATIosApplicationBridge.h"
#import "apptraverse/platform/apple/apple_window_layout.h"

#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/ios/ios_application_bridge.h"
#include "apptraverse/platform/ios/ios_ui_native_views.h"
#include "apptraverse/platform/ios/ios_ui_presenters.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

using apptraverse::ui::UiDeviceEnvironment;
using apptraverse::ui::UiDeviceState;
using apptraverse::ui::UiEnvironmentReason;
using apptraverse::ui::UiWindowEnvironment;
using apptraverse::ui::ios::IosDesktopWindowPresenter;
using apptraverse::ui::ios::IosListContainerPresenter;
using apptraverse::ui::ios::IosPushButtonPresenter;

namespace {

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

class IosNativeViews final
    : public apptraverse::ui::ios::IosUiNativeViews {
 public:
  explicit IosNativeViews(ATIosApplicationBridge* owner) : owner_{owner} {}

  void LoadDesktopWindow(IosDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadWindow:Token(presenter)];
  }
  void SyncDesktopWindow(IosDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncWindow:Token(presenter)];
  }
  void UnloadDesktopWindow(IosDesktopWindowPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadWindow:Token(presenter)];
  }
  void LoadPushButton(IosPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadButton:Token(presenter)];
  }
  void SyncPushButton(IosPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncButton:Token(presenter)];
  }
  void UnloadPushButton(IosPushButtonPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadButton:Token(presenter)];
  }
  void LoadListContainer(IosListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ loadList:Token(presenter)];
  }
  void SyncListContainer(IosListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ syncList:Token(presenter)];
  }
  void UnloadListContainer(IosListContainerPresenter& presenter) override {
    [owner_.delegate bridge:owner_ unloadList:Token(presenter)];
  }

 private:
  __unsafe_unretained ATIosApplicationBridge* owner_;
};

}  // namespace

@implementation ATIosMenuItemState
- (instancetype)initWithTitle:(NSString*)title selected:(BOOL)selected {
  if ((self = [super init])) { _title = title; _selected = selected; }
  return self;
}
@end

@implementation ATIosMenuGroupState
- (instancetype)initWithTitle:(NSString*)title
                        items:(NSArray<ATIosMenuItemState*>*)items {
  if ((self = [super init])) { _title = title; _items = [items copy]; }
  return self;
}
@end

@implementation ATIosSettingsState
- (instancetype)initWithTitle:(NSString*)title doneTitle:(NSString*)doneTitle
                       groups:(NSArray<ATIosMenuGroupState*>*)groups
                    presented:(BOOL)presented {
  if ((self = [super init])) {
    _title = title;
    _doneTitle = doneTitle;
    _groups = [groups copy];
    _presented = presented;
  }
  return self;
}
@end

@interface ATIosWindowState ()
- (instancetype)initWithTitle:(NSString*)title identity:(uint64_t)identity
                      layout:(ATAppleWindowLayout*)layout theme:(NSUInteger)theme
                  softenDark:(BOOL)softenDark;
@end

@implementation ATIosWindowState
- (instancetype)initWithTitle:(NSString*)title identity:(uint64_t)identity
                      layout:(ATAppleWindowLayout*)layout theme:(NSUInteger)theme
                  softenDark:(BOOL)softenDark {
  if ((self = [super init])) {
    _title = title;
    _identity = identity;
    _layout = layout;
    _theme = theme;
    _softenDark = softenDark;
  }
  return self;
}
@end

@interface ATIosButtonState ()
- (instancetype)initWithParentToken:(uint64_t)parentToken
                              title:(NSString*)title
                            enabled:(BOOL)enabled
                          controlID:(uint32_t)controlID;
@end

@implementation ATIosButtonState
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

@interface ATIosListRowState ()
- (instancetype)initWithIdentity:(uint64_t)identity text:(NSString*)text;
@end

@implementation ATIosListRowState
- (instancetype)initWithIdentity:(uint64_t)identity text:(NSString*)text {
  if ((self = [super init])) {
    _identity = identity;
    _text = text;
  }
  return self;
}
@end

@interface ATIosListState ()
- (instancetype)initWithParentToken:(uint64_t)parentToken
                               rows:(NSArray<ATIosListRowState*>*)rows
                        selectedRow:(NSInteger)selectedRow
                          controlID:(uint32_t)controlID;
@end

@implementation ATIosListState
- (instancetype)initWithParentToken:(uint64_t)parentToken
                               rows:(NSArray<ATIosListRowState*>*)rows
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

@interface ATIosApplicationBridge () {
  apptraverse::ui::ios::IosApplicationBridgeCallbacks _callbacks;
  apptraverse::ApplicationModelRuntime _ownedRuntime;
  apptraverse::ApplicationModelRuntime* _runtime;
  std::unique_ptr<apptraverse::ModelObjectProxy> _proxy;
  std::unique_ptr<IosNativeViews> _nativeViews;
  std::unique_ptr<apptraverse::ApplicationUiSession> _uiSession;
  std::thread _modelThread;
  int _modelExitStatus;
  bool _stopping;
  std::int32_t _viewportWidth;
  std::int32_t _viewportHeight;
  uint64_t _activeWindowToken;
  UiDeviceEnvironment _deviceEnvironment;
  UiDeviceEnvironment _submittedDeviceEnvironment;
  std::optional<UiWindowEnvironment> _submittedViewport;
}
@property(nonatomic, weak, readwrite)
    id<ATIosApplicationBridgeDelegate> delegate;
- (instancetype)initWithCallbacks:
                    (apptraverse::ui::ios::IosApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATIosApplicationBridgeDelegate>)delegate;
- (void)publishMobilePresentation;
- (void)observeMobileViewport;
- (void)publishDeviceSettings;
@end

@implementation ATIosApplicationBridge

- (instancetype)initWithCallbacks:
                    (apptraverse::ui::ios::IosApplicationBridgeCallbacks)
                        callbacks
                         delegate:(id<ATIosApplicationBridgeDelegate>)delegate {
  if ((self = [super init])) {
    _callbacks = std::move(callbacks);
    _delegate = delegate;
    _modelExitStatus = 1;
    _runtime = _callbacks.runtime ? _callbacks.runtime : &_ownedRuntime;
    _stopping = false;
    _viewportWidth = 0;
    _viewportHeight = 0;
  }
  return self;
}

- (void)start {
  _proxy = std::make_unique<apptraverse::ModelObjectProxy>(
      [self](apptraverse::ModelObjectProxy::ModelWork work) {
        _runtime->Post(std::move(work));
      });
  _nativeViews = std::make_unique<IosNativeViews>(self);
  _submittedDeviceEnvironment = _deviceEnvironment;
  _modelThread = std::thread([self, environment = _deviceEnvironment] {
    _modelExitStatus = _callbacks.run_model(
        *_runtime, [self](apptraverse::ApplicationPublicationKind kind) {
          dispatch_async(dispatch_get_main_queue(), ^{
            [self
                consumePublication:
                    kind == apptraverse::ApplicationPublicationKind::Initial
                        ? ATIosPublicationKindInitial
                        : ATIosPublicationKindIncremental];
          });
        }, environment);
    dispatch_async(dispatch_get_main_queue(), ^{
      id<ATIosApplicationBridgeDelegate> delegate = self.delegate;
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

- (void)requestBackgroundSave {
  if (_stopping) return;
  // Post also accepts work before Run's initial publication. The callback runs
  // only after application preparation, behind previously accepted commands,
  // on the same model thread and Domain as the continuing session.
  _runtime->Post([self](ae::Domain& domain) {
    int const result = _callbacks.persist_model(domain);
    dispatch_async(dispatch_get_main_queue(), ^{
      [self.delegate bridge:self backgroundSaveDidFinish:result];
    });
  });
}

- (void)publishMobilePresentation {
  auto const snapshot = _callbacks.mobile_presentation(_uiSession->root());
  std::vector<ae::Obj*> objects;
  apptraverse::CollectLiveReachableObjects(_uiSession->root(), objects);
  auto& registry = ae::Registry::GetRegistry();
  auto token_for = [&](ae::ObjId id) -> uint64_t {
    for (auto* object : objects) {
      if (registry.GenerationDistance(IosDesktopWindowPresenter::kClassId,
                                      object->GetClassId()) < 0) continue;
      auto& presenter = *static_cast<IosDesktopWindowPresenter*>(object);
      if (presenter.presentation_loaded && presenter.window.id() == id) {
        return Token(presenter);
      }
    }
    apptraverse::WriteFatalStderr("fatal: iOS mobile page has no live presenter\n");
    std::abort();
  };
  NSMutableArray<NSNumber*>* pages = [NSMutableArray arrayWithCapacity:snapshot.windows.size()];
  for (auto id : snapshot.windows) [pages addObject:@(token_for(id))];
  _activeWindowToken = token_for(snapshot.active_window);
  [self.delegate bridge:self applyMobilePages:pages activeToken:_activeWindowToken];
  [self publishDeviceSettings];
  [self observeMobileViewport];
}

- (void)observeMobileViewport {
  if (_stopping || !_activeWindowToken || _viewportWidth < 1 || _viewportHeight < 1) return;
  auto& presenter = FromToken<IosDesktopWindowPresenter>(_activeWindowToken);
  auto const dpi = _deviceEnvironment.system_dpi_x;
  UiWindowEnvironment const viewport{presenter.window.id(), {}, dpi, dpi,
                                     {0, 0, _viewportWidth, _viewportHeight}};
  if (_submittedViewport == viewport) return;
  _submittedViewport = viewport;
  _proxy->Invoke(presenter.LocalDeviceState()->obj_id, &UiDeviceState::ObserveWindow,
                 viewport, UiEnvironmentReason::kSystemNotification);
}

- (void)observeDeviceSettings:(ATIosDeviceSettings)settings {
  if (_stopping) return;
  auto const environment = apptraverse::ui::ios::IosDeviceEnvironment(settings);
  if (environment == _deviceEnvironment) return;
  _deviceEnvironment = environment;
  [self publishDeviceSettings];
  [self observeMobileViewport];
}

- (void)publishDeviceSettings {
  if (_stopping || !_activeWindowToken || _submittedDeviceEnvironment == _deviceEnvironment)
    return;
  _submittedDeviceEnvironment = _deviceEnvironment;
  auto& presenter = FromToken<IosDesktopWindowPresenter>(_activeWindowToken);
  _proxy->Invoke(presenter.LocalDeviceState()->obj_id, &UiDeviceState::ObserveEnvironment,
                 _deviceEnvironment, UiEnvironmentReason::kSystemNotification);
}

- (void)consumePublication:(ATIosPublicationKind)kind {
  if (self.delegate == nil) {
    // Host destruction is a teardown state, not a partially loaded UI. Drain
    // the publication slot so the stopped model can finish without a host.
    [self requestStop];
    _runtime->AbandonUnconsumedPublicationsForTeardown();
    return;
  }
  std::vector<std::uint8_t> publication =
      _runtime->TakePublishedCopyForUiApply(
          kind == ATIosPublicationKindInitial
              ? apptraverse::TakePublishedSidecarCallback{}
              : _callbacks.take_incremental_sidecar);
  _activeWindowToken = 0;
  if (kind == ATIosPublicationKindInitial) {
    _uiSession = std::make_unique<apptraverse::ApplicationUiSession>(
        publication, _nativeViews.get(), *_proxy,
        _callbacks.prepare_ui_presenters);
    if (_callbacks.after_initial) _callbacks.after_initial(_uiSession->root());
  } else {
    _uiSession->ApplyStructural(publication);
    if (_callbacks.after_incremental) _callbacks.after_incremental(_uiSession->root());
  }
  [self publishMobilePresentation];
  if (_callbacks.settings_state) {
    [self.delegate bridge:self syncSettings:_callbacks.settings_state(_uiSession->root())];
  }
  [self.delegate bridgeDidFinishPublication:self];
  _runtime->NotifyPublicationConsumed();
}

- (ATIosWindowState*)windowState:(uint64_t)token {
  auto& presenter = FromToken<IosDesktopWindowPresenter>(token);
  auto const appearance = presenter.LocalDeviceState()->appearance;
  NSString* title = ToNSString(presenter.ResolvePresentedWindowTitle(
      presenter.locale->language_index()));
  return [[ATIosWindowState alloc]
      initWithTitle:title
           identity:presenter.window.id().id()
             layout:[[ATAppleWindowLayout alloc] initWithRoot:*presenter.LayoutRoot()]
              theme:static_cast<NSUInteger>(appearance.theme)
         softenDark:appearance.soften_dark];
}

- (ATIosButtonState*)buttonState:(uint64_t)token {
  auto& presenter = FromToken<IosPushButtonPresenter>(token);
  NSString* title = ToNSString(presenter.button->label->Resolve(
      presenter.window_presenter->locale->language_index()));
  return [[ATIosButtonState alloc]
      initWithParentToken:Token(*presenter.window_presenter)
                    title:title
                  enabled:presenter.button->enabled()
                controlID:presenter.control_id];
}

- (ATIosListState*)listState:(uint64_t)token {
  auto& presenter = FromToken<IosListContainerPresenter>(token);
  int const language_index =
      presenter.window_presenter->locale->language_index();
  NSMutableArray<ATIosListRowState*>* rows =
      [[NSMutableArray alloc] initWithCapacity:presenter.container->rows.size()];
  for (auto const& row : presenter.container->rows) {
    auto const identity = static_cast<std::uint64_t>(row.id().id());
    [rows addObject:[[ATIosListRowState alloc]
                        initWithIdentity:identity
                                   text:ToNSString(
                                            presenter.ResolveListRowPrimaryText(
                                                    row, language_index))]];
  }
  return [[ATIosListState alloc]
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

- (void)submitSettingsPresented:(BOOL)presented {
  if (_stopping || !_uiSession || !_callbacks.on_settings_presented) return;
  _callbacks.on_settings_presented(_uiSession->root(), *_proxy, presented);
}

- (void)clickButton:(uint64_t)token {
  if (_stopping) return;
  FromToken<IosPushButtonPresenter>(token).NotifyClickedFromUi();
}

- (void)selectListRow:(NSInteger)row token:(uint64_t)token {
  if (_stopping) return;
  FromToken<IosListContainerPresenter>(token)
      .SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(row));
}

- (void)activateListRow:(NSInteger)row token:(uint64_t)token {
  if (_stopping) return;
  FromToken<IosListContainerPresenter>(token)
      .SubmitListRowActivatedFromUi(static_cast<std::size_t>(row));
}

- (void)submitViewportWidth:(double)width height:(double)height {
  if (_stopping) return;
  if (!std::isfinite(width) || !std::isfinite(height) ||
      width > std::numeric_limits<std::int32_t>::max() ||
      height > std::numeric_limits<std::int32_t>::max()) {
    apptraverse::WriteFatalStderr("invalid native iOS viewport\n");
    std::abort();
  }
  if (width < 1 || height < 1) return;
  _viewportWidth = static_cast<std::int32_t>(width);
  _viewportHeight = static_cast<std::int32_t>(height);
  [self observeMobileViewport];
}

- (void)submitMobileSwipe:(NSInteger)delta {
  if (_stopping || !_uiSession || !_callbacks.on_mobile_swipe) return;
  _callbacks.on_mobile_swipe(_uiSession->root(), *_proxy, static_cast<int>(delta));
}

- (void)submitMobilePage:(uint64_t)token {
  if (_stopping || !_uiSession || !_callbacks.on_mobile_page) return;
  auto& presenter = FromToken<IosDesktopWindowPresenter>(token);
  _callbacks.on_mobile_page(_uiSession->root(), *_proxy, presenter.window.id());
}

- (BOOL)requestCloseWindow:(uint64_t)token {
  if (_stopping) return YES;
  auto& presenter = FromToken<IosDesktopWindowPresenter>(token);
  bool const stops_application = _callbacks.close_desktop_window(
      _uiSession->root(), *presenter.window, *_proxy,
      [self] { _runtime->RequestStop(); });
  _stopping = stops_application;
  return stops_application;
}

- (void)requestStop {
  if (_stopping) return;
  _stopping = true;
  _runtime->RequestStop();
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

namespace apptraverse::ui::ios {

UiDeviceEnvironment IosDeviceEnvironment(ATIosDeviceSettings settings) {
  UiDeviceEnvironment environment;
  environment.supported_settings = kSystemTextScale | kSystemDarkMode |
      kSystemHighContrast | kSystemReducedMotion | kSystemScreenReaderHint;
  environment.system_dpi_x = environment.system_dpi_y =
      static_cast<unsigned>(std::lround(96 * settings.displayScale));
  environment.text_scale_milli =
      static_cast<unsigned>(std::lround(1000 * settings.bodyTextScale));
  environment.dark = settings.dark;
  environment.high_contrast = settings.highContrast;
  environment.reduced_motion = settings.reducedMotion;
  environment.screen_reader_hint = settings.voiceOver;
  return environment;
}

ATIosApplicationBridge* MakeIosApplicationBridge(
    IosApplicationBridgeCallbacks callbacks,
    id<ATIosApplicationBridgeDelegate> delegate) {
  return [[ATIosApplicationBridge alloc] initWithCallbacks:std::move(callbacks)
                                                 delegate:delegate];
}

}  // namespace apptraverse::ui::ios
