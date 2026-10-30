#import "apptraverse/platform/apple/AppleCatalogBridge.h"

#include "apptraverse/platform/apple/apple_presentation_host.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::ui::apple {
namespace {

[[noreturn]] void FatalApple(char const* message) {
  WriteFatalStderr(message);
  std::abort();
}

AppleCatalogProjection* Projection(void* projection) {
  if (projection == nullptr) {
    FatalApple("fatal: Apple catalog projection missing\n");
  }
  return (__bridge AppleCatalogProjection*)projection;
}

std::uint64_t NumericId(ae::ObjId id) {
  return static_cast<std::uint64_t>(id.id());
}

std::string LocalizedText(ae::ObjPtr<UiLocalizationString>& text, int language) {
  if (!text) {
    return {};
  }
  if (!text.is_loaded()) {
    text.Load();
  }
  return text->Resolve(language);
}

int LanguageOf(UiWindowPresenter& presenter) {
  if (!presenter.locale) {
    FatalApple("fatal: window presenter locale missing\n");
  }
  if (!presenter.locale.is_loaded()) {
    presenter.locale.Load();
  }
  return presenter.locale->language_index();
}

std::uint64_t WindowIdOf(UiWindowPresenter& presenter) {
  if (!presenter.window) {
    FatalApple("fatal: window presenter window missing\n");
  }
  if (!presenter.window.is_loaded()) {
    presenter.window.Load();
  }
  return NumericId(presenter.window->obj_id);
}

AppleWindowProjection* FindWindow(AppleCatalogProjection* projection,
                                  std::uint64_t window_id) {
  for (AppleWindowProjection* window in projection.windows) {
    if (window.windowId == window_id) {
      return window;
    }
  }
  return nil;
}

AppleWindowProjection* EnsureWindow(AppleCatalogProjection* projection,
                                    std::uint64_t window_id) {
  AppleWindowProjection* existing = FindWindow(projection, window_id);
  if (existing != nil) {
    return existing;
  }
  AppleWindowProjection* created = [[AppleWindowProjection alloc] init];
  created.windowId = window_id;
  created.title = @"";
  created.rowTitles = @[];
  created.rowIds = @[];
  created.buttons = @[];
  created.selection = -1;
  NSMutableArray<AppleWindowProjection*>* windows =
      [projection.windows mutableCopy];
  [windows addObject:created];
  projection.windows = windows;
  return created;
}

void PublishProjection(AppleCatalogProjection* projection) {
  projection.windows = [projection.windows copy];
  [projection noteChanged];
}

}  // namespace

ApplePresentationHost* AppleRequireHost(void* presentation_host) {
  if (presentation_host == nullptr) {
    FatalApple("fatal: Apple presentation host missing\n");
  }
  return static_cast<ApplePresentationHost*>(presentation_host);
}

void AppleProjectionSetSessionLabel(void* projection, char const* label) {
  AppleCatalogProjection* view = Projection(projection);
  view.sessionLabel = [NSString stringWithUTF8String:label != nullptr ? label : ""];
  [view noteChanged];
}

void AppleProjectionBumpRevision(void* projection) {
  AppleCatalogProjection* view = Projection(projection);
  view.revision += 1;
  PublishProjection(view);
}

void AppleBindPresentationHost(ApplePresentationHost* host) {
  if (host == nullptr || host->projection == nullptr) {
    FatalApple("fatal: cannot bind Apple presentation host\n");
  }
  AppleCatalogProjection* projection = Projection(host->projection);
  projection.onButton = ^(uint64_t window_id, uint32_t control_id) {
    (void)window_id;
    auto const found = host->buttons_by_control.find(control_id);
    if (found == host->buttons_by_control.end() || found->second == nullptr) {
      return;
    }
    found->second->NotifyClickedFromUi();
  };
  projection.onSelectRow = ^(uint64_t window_id, NSInteger row) {
    (void)window_id;
    if (host->list == nullptr || row < 0) {
      return;
    }
    host->list->SubmitListRowSelectionChangedFromUi(
        static_cast<std::size_t>(row));
  };
  projection.onCloseWindow = ^(uint64_t window_id) {
    auto const found = host->windows_by_id.find(window_id);
    if (found == host->windows_by_id.end() || found->second == nullptr) {
      return;
    }
    AppleCloseDesktopPresenter(host, *found->second);
  };
  projection.onSave = ^{
    if (!host->request_persist) {
      return;
    }
    host->request_persist([host](int status) {
      dispatch_async(dispatch_get_main_queue(), ^{
        AppleCatalogProjection* view = Projection(host->projection);
        view.saveStatus = status == 0 ? @"confirmed" : @"failed";
        [view noteChanged];
      });
    });
  };
  projection.onViewport = ^(int32_t width, int32_t height) {
    AppleCatalogProjection* view = Projection(host->projection);
    view.viewportLabel =
        [NSString stringWithFormat:@"%dx%d", width, height];
    [view noteChanged];
  };
}

void AppleUnbindPresentationHost(void* projection) {
  if (projection == nullptr) {
    return;
  }
  AppleCatalogProjection* view = (__bridge AppleCatalogProjection*)projection;
  view.onChanged = nil;
  view.onButton = nil;
  view.onSelectRow = nil;
  view.onCloseWindow = nil;
  view.onSave = nil;
  view.onViewport = nil;
}

void AppleCloseDesktopPresenter(ApplePresentationHost* host,
                                UiDesktopWindowPresenter& presenter) {
  if (host->on_desktop_window_user_close == nullptr) {
    FatalApple("fatal: desktop close callback missing\n");
  }
  auto desktop = presenter.desktop_window();
  if (!desktop) {
    FatalApple("fatal: desktop window missing on close\n");
  }
  host->on_desktop_window_user_close(host->opaque, &*desktop);
}

void AppleSyncDesktopPresenter(UiDesktopWindowPresenter& presenter) {
  ApplePresentationHost* host = AppleRequireHost(presenter.presentation_host);
  auto desktop = presenter.desktop_window();
  if (!desktop) {
    FatalApple("fatal: desktop window missing\n");
  }
  std::uint64_t const window_id = NumericId(desktop->obj_id);
  host->windows_by_id[window_id] = &presenter;
  AppleCatalogProjection* projection = Projection(host->projection);
  AppleWindowProjection* window = EnsureWindow(projection, window_id);
  std::string const title = LocalizedText(presenter.title, LanguageOf(presenter));
  window.title = [NSString stringWithUTF8String:title.c_str()];
  window.frameX = desktop->x();
  window.frameY = desktop->y();
  window.frameWidth = desktop->client_width();
  window.frameHeight = desktop->client_height();
  PublishProjection(projection);
}

void AppleRemoveDesktopPresenter(UiDesktopWindowPresenter& presenter) {
  ApplePresentationHost* host = AppleRequireHost(presenter.presentation_host);
  auto desktop = presenter.desktop_window();
  if (!desktop) {
    return;
  }
  std::uint64_t const window_id = NumericId(desktop->obj_id);
  auto const found = host->windows_by_id.find(window_id);
  if (found != host->windows_by_id.end() && found->second == &presenter) {
    host->windows_by_id.erase(found);
  }
  AppleCatalogProjection* projection = Projection(host->projection);
  NSMutableArray<AppleWindowProjection*>* windows =
      [projection.windows mutableCopy];
  NSUInteger const index = [windows indexOfObjectPassingTest:^BOOL(
                                AppleWindowProjection* window, NSUInteger,
                                BOOL*) {
    return window.windowId == window_id;
  }];
  if (index != NSNotFound) {
    [windows removeObjectAtIndex:index];
  }
  projection.windows = windows;
  PublishProjection(projection);
}

void AppleSyncListPresenter(UiListContainerPresenter& presenter) {
  ApplePresentationHost* host = AppleRequireHost(presenter.presentation_host);
  host->list = &presenter;
  if (!presenter.container) {
    FatalApple("fatal: list container missing\n");
  }
  if (!presenter.container.is_loaded()) {
    presenter.container.Load();
  }
  if (!presenter.window_presenter) {
    FatalApple("fatal: list window presenter missing\n");
  }
  if (!presenter.window_presenter.is_loaded()) {
    presenter.window_presenter.Load();
  }
  int const language = LanguageOf(*presenter.window_presenter);
  std::uint64_t const window_id = WindowIdOf(*presenter.window_presenter);
  AppleCatalogProjection* projection = Projection(host->projection);
  AppleWindowProjection* window = EnsureWindow(projection, window_id);
  window.catalogChrome = YES;
  window.selection = presenter.container->selection_index;
  NSMutableArray<NSString*>* titles = [NSMutableArray array];
  NSMutableArray<NSString*>* ids = [NSMutableArray array];
  for (auto& row : presenter.container->rows) {
    if (!row) {
      FatalApple("fatal: catalog list row missing\n");
    }
    if (!row.is_loaded()) {
      row.Load();
    }
    std::string const label = LocalizedText(row->primary_text, language);
    [titles addObject:[NSString stringWithUTF8String:label.c_str()]];
    [ids addObject:[NSString stringWithFormat:@"%llu",
                    static_cast<unsigned long long>(NumericId(row->obj_id))]];
  }
  window.rowTitles = titles;
  window.rowIds = ids;
  PublishProjection(projection);
}

void AppleSyncButtonPresenter(UiPushButtonPresenter& presenter) {
  ApplePresentationHost* host = AppleRequireHost(presenter.presentation_host);
  host->buttons_by_control[presenter.control_id] = &presenter;
  if (!presenter.button) {
    FatalApple("fatal: push button missing\n");
  }
  if (!presenter.button.is_loaded()) {
    presenter.button.Load();
  }
  if (!presenter.window_presenter) {
    FatalApple("fatal: button window presenter missing\n");
  }
  if (!presenter.window_presenter.is_loaded()) {
    presenter.window_presenter.Load();
  }
  int const language = LanguageOf(*presenter.window_presenter);
  std::uint64_t const window_id = WindowIdOf(*presenter.window_presenter);
  std::string const title =
      LocalizedText(presenter.button->label, language);
  AppleCatalogProjection* projection = Projection(host->projection);
  AppleWindowProjection* window = EnsureWindow(projection, window_id);
  NSMutableArray<AppleButtonProjection*>* buttons =
      [window.buttons mutableCopy];
  if (buttons == nil) {
    buttons = [NSMutableArray array];
  }
  AppleButtonProjection* slot = nil;
  for (AppleButtonProjection* button in buttons) {
    if (button.controlId == presenter.control_id) {
      slot = button;
      break;
    }
  }
  if (slot == nil) {
    slot = [[AppleButtonProjection alloc] init];
    slot.controlId = presenter.control_id;
    [buttons addObject:slot];
  }
  slot.title = [NSString stringWithUTF8String:title.c_str()];
  slot.enabled = presenter.button->enabled() ? YES : NO;
  window.buttons = buttons;
  PublishProjection(projection);
}

}  // namespace apptraverse::ui::apple

@implementation AppleButtonProjection
@end

@implementation AppleWindowProjection

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _title = @"";
    _selection = -1;
    _rowTitles = @[];
    _rowIds = @[];
    _buttons = @[];
  }
  return self;
}

@end

@implementation AppleCatalogProjection

- (instancetype)init {
  self = [super init];
  if (self != nil) {
    _windows = @[];
    _saveStatus = @"";
    _viewportLabel = @"";
    _sessionLabel = @"";
  }
  return self;
}

- (void)noteChanged {
  if (self.onChanged != nil) {
    self.onChanged();
  }
}

@end
