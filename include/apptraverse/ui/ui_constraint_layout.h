#ifndef APPTRAVERSE_UI_UI_CONSTRAINT_LAYOUT_H_
#define APPTRAVERSE_UI_UI_CONSTRAINT_LAYOUT_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse::ui {

// A portable, structured constraint vocabulary. These values are part of the
// distilled UI description; they contain no platform handles or measured pixels.
// This is a stack/container layout engine, not an arbitrary linear-anchor solver.
enum class UiLayoutFlow : std::uint8_t {
  kLeaf,
  kHorizontal,
  kVertical,
  kAdaptive,
};

enum class UiLayoutAlignment : std::uint8_t {
  kStretch,
  kStart,
  kCenter,
  kEnd,
};

struct UiLayoutNode {
  // A stable control slot in its window, not an object identity. Containers use 0.
  std::uint32_t control_id{0};
  UiLayoutFlow flow{UiLayoutFlow::kLeaf};
  double min_width{0};
  double min_height{0};
  double preferred_width{0};
  double preferred_height{0};
  // A negative maximum means unbounded. Minimums are required; preferred sizes
  // may compress down to the minimum. grow shares spare primary-axis space.
  double max_width{-1};
  double max_height{-1};
  double grow{0};
  double padding{0};
  double gap{0};
  UiLayoutAlignment alignment{UiLayoutAlignment::kStretch};
  std::vector<UiLayoutNode> children;

  AE_REFLECT_MEMBERS(control_id, flow, min_width, min_height, preferred_width,
                    preferred_height, max_width, max_height, grow, padding, gap,
                    alignment, children)
};

// Local intrinsic measurements, expressed in the same logical units as the
// viewport. The backend must remove its display scaling exactly once.
struct UiLayoutMeasurement {
  std::uint32_t control_id{0};
  double min_width{0};
  double min_height{0};
  double preferred_width{0};
  double preferred_height{0};
};

struct UiLayoutRect {
  std::uint32_t control_id{0};
  double x{0};
  double y{0};
  double width{0};
  double height{0};
};

struct UiConstraintLayoutResult {
  bool valid{true};
  std::string error;
  std::vector<UiLayoutRect> controls;
  double content_width{0};
  double content_height{0};
  bool overflow_x{false};
  bool overflow_y{false};
};

// Optional width-dependent text measurement. Called with the allocated logical
// width, allowing a backend to report the height of wrapped text. It must be
// side-effect-free: do not move controls or mutate the model while measuring.
using UiLayoutRemeasure =
    std::function<UiLayoutMeasurement(std::uint32_t, double)>;

// Validate a distilled definition or an Event payload before replacing the
// current layout. No native measurements are required. An invalid replacement
// must not be committed to the model; error receives an actionable diagnostic.
[[nodiscard]] bool ValidateUiConstraintLayout(UiLayoutNode const& root,
                                             std::string* error = nullptr);

// Layout is evaluated locally from a UI mirror. kAdaptive is supported at the
// root only and uses horizontal flow when viewport width >= height, vertical
// otherwise. Nested adaptive nodes are rejected, rather than selecting flow
// from an ancestor's dimensions before sibling space allocation. Invalid/contradictory
// required bounds return valid=false and error. If a viewport is smaller than required
// minimums, the result preserves controls and reports a scrollable content extent
// instead of silently shrinking text or touch targets.
[[nodiscard]] UiConstraintLayoutResult ComputeUiConstraintLayout(
    UiLayoutNode const& root, double viewport_width, double viewport_height,
    std::vector<UiLayoutMeasurement> const& measurements,
    UiLayoutRemeasure const& remeasure = {});

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_CONSTRAINT_LAYOUT_H_
