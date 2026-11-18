#import "apptraverse/platform/apple/apple_window_layout.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <utility>

#include "apptraverse/noninteractive_crt.h"

@interface ATAppleLayoutResult () {
  std::vector<apptraverse::ui::UiLayoutRect> _controls;
}
- (instancetype)initWithResult:(apptraverse::ui::UiConstraintLayoutResult)result;
@end

@implementation ATAppleLayoutResult
- (instancetype)initWithResult:(apptraverse::ui::UiConstraintLayoutResult)result {
  if ((self = [super init])) {
    _contentSize = CGSizeMake(result.content_width, result.content_height);
    _controls = std::move(result.controls);
  }
  return self;
}

- (CGRect)frameForControl:(uint32_t)controlID {
  auto const frame = std::find_if(_controls.begin(), _controls.end(),
      [&](auto const& rect) { return rect.control_id == controlID; });
  if (frame == _controls.end()) {
    apptraverse::WriteFatalStderr("missing solved Apple control frame\n");
    std::abort();
  }
  return CGRectMake(frame->x, frame->y, frame->width, frame->height);
}
@end

@interface ATAppleWindowLayout () {
  apptraverse::ui::UiLayoutNode _root;
}
@end

@implementation ATAppleWindowLayout
- (instancetype)initWithRoot:(apptraverse::ui::UiLayoutNode const&)root {
  if ((self = [super init])) _root = root;
  return self;
}

- (ATAppleLayoutResult*)solveViewport:(CGSize)viewport
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
      apptraverse::WriteFatalStderr("duplicate or unknown Apple layout control\n");
      std::abort();
    }
    auto const ideal = measure(id, -1);
    auto const minimum = measure(id, 0);
    measurements.push_back({id, minimum.width, ideal.height,
                            ideal.width, ideal.height});
  }
  if (!slots.empty()) {
    std::string detail = "missing required Apple layout controls:";
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
  return [[ATAppleLayoutResult alloc] initWithResult:std::move(result)];
}
@end
