#include "apptraverse/ui/ui_constraint_layout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace apptraverse::ui {
namespace {

constexpr double kEpsilon = 0.000001;
constexpr std::size_t kMaxNodes = 16384;
constexpr std::size_t kMaxDepth = 128;
constexpr int kMaxMeasurePasses = 8;

double Maximum(double value) {
  return value < 0 ? std::numeric_limits<double>::infinity() : value;
}

bool IsNonnegative(double value) {
  return std::isfinite(value) && value >= 0;
}

bool ValidateMeasurement(UiLayoutMeasurement const& value) {
  if (value.control_id == 0) {
    return false;
  }
  for (double size : {value.min_width, value.min_height, value.preferred_width,
                      value.preferred_height}) {
    if (!IsNonnegative(size)) return false;
  }
  return true;
}

struct PreparedNode {
  UiLayoutNode const* spec{nullptr};
  bool valid{true};
  std::string error;
  bool horizontal{false};
  double min_width{0};
  double min_height{0};
  double preferred_width{0};
  double preferred_height{0};
  std::vector<PreparedNode> children;
};

using Measurements = std::unordered_map<std::uint32_t, UiLayoutMeasurement>;

PreparedNode InvalidPrepared(char const* error) {
  PreparedNode result;
  result.valid = false;
  result.error = error;
  return result;
}

UiConstraintLayoutResult InvalidResult(std::string error) {
  UiConstraintLayoutResult result;
  result.valid = false;
  result.error = std::move(error);
  return result;
}

PreparedNode Prepare(UiLayoutNode const& spec, double offered_width,
                     double offered_height, Measurements const& measurements,
                     std::unordered_set<std::uint32_t>& ids,
                     std::size_t& node_count, std::size_t depth) {
  if (++node_count > kMaxNodes || depth > kMaxDepth) {
    return InvalidPrepared("layout tree exceeds supported complexity");
  }
  for (double value : {spec.min_width, spec.min_height, spec.preferred_width,
                       spec.preferred_height, spec.grow, spec.padding, spec.gap}) {
    if (!IsNonnegative(value)) {
      return InvalidPrepared("layout requires finite nonnegative dimensions");
    }
  }
  if (!std::isfinite(spec.max_width) || !std::isfinite(spec.max_height) ||
      spec.max_width < -1 || spec.max_height < -1 ||
      (spec.max_width < 0 && spec.max_width != -1) ||
      (spec.max_height < 0 && spec.max_height != -1)) {
    return InvalidPrepared("layout maximum must be nonnegative or -1");
  }
  if (spec.flow != UiLayoutFlow::kLeaf &&
      spec.flow != UiLayoutFlow::kHorizontal &&
      spec.flow != UiLayoutFlow::kVertical &&
      spec.flow != UiLayoutFlow::kAdaptive) {
    return InvalidPrepared("unknown layout flow");
  }
  if (spec.flow == UiLayoutFlow::kAdaptive && depth != 0) {
    return InvalidPrepared("adaptive flow is supported only at the layout root");
  }
  if (spec.alignment != UiLayoutAlignment::kStretch &&
      spec.alignment != UiLayoutAlignment::kStart &&
      spec.alignment != UiLayoutAlignment::kCenter &&
      spec.alignment != UiLayoutAlignment::kEnd) {
    return InvalidPrepared("unknown layout alignment");
  }

  PreparedNode out;
  out.spec = &spec;
  out.min_width = spec.min_width;
  out.min_height = spec.min_height;
  out.preferred_width = spec.preferred_width;
  out.preferred_height = spec.preferred_height;
  if (spec.flow == UiLayoutFlow::kLeaf) {
    if (!spec.children.empty() || spec.control_id == 0 ||
        !ids.insert(spec.control_id).second) {
      return InvalidPrepared("layout leaf requires a unique control slot");
    }
    auto found = measurements.find(spec.control_id);
    if (found == measurements.end()) {
      return InvalidPrepared("layout leaf has no intrinsic measurement");
    }
    auto const& value = found->second;
    out.min_width = std::max(out.min_width, value.min_width);
    out.min_height = std::max(out.min_height, value.min_height);
    out.preferred_width = std::max(out.preferred_width, value.preferred_width);
    out.preferred_height = std::max(out.preferred_height, value.preferred_height);
  } else {
    if (spec.control_id != 0) {
      return InvalidPrepared("layout container cannot bind a native control");
    }
    out.horizontal = spec.flow == UiLayoutFlow::kHorizontal ||
                     (spec.flow == UiLayoutFlow::kAdaptive &&
                      offered_width >= offered_height);
    double const inner_width = std::max(0.0, offered_width - 2 * spec.padding);
    double const inner_height = std::max(0.0, offered_height - 2 * spec.padding);
    double min_primary = 0, preferred_primary = 0;
    double min_cross = 0, preferred_cross = 0;
    for (auto const& child : spec.children) {
      auto prepared = Prepare(child, inner_width, inner_height, measurements,
                              ids, node_count, depth + 1);
      if (!prepared.valid) return prepared;
      min_primary += out.horizontal ? prepared.min_width : prepared.min_height;
      preferred_primary += out.horizontal ? prepared.preferred_width
                                          : prepared.preferred_height;
      min_cross = std::max(min_cross, out.horizontal ? prepared.min_height
                                                    : prepared.min_width);
      preferred_cross = std::max(preferred_cross,
                                out.horizontal ? prepared.preferred_height
                                               : prepared.preferred_width);
      out.children.push_back(std::move(prepared));
    }
    double const gaps = spec.children.empty()
                            ? 0
                            : spec.gap * (spec.children.size() - 1);
    min_primary += gaps + 2 * spec.padding;
    preferred_primary += gaps + 2 * spec.padding;
    min_cross += 2 * spec.padding;
    preferred_cross += 2 * spec.padding;
    out.min_width = std::max(out.min_width,
                            out.horizontal ? min_primary : min_cross);
    out.min_height = std::max(out.min_height,
                             out.horizontal ? min_cross : min_primary);
    out.preferred_width = std::max(out.preferred_width,
                                  out.horizontal ? preferred_primary : preferred_cross);
    out.preferred_height = std::max(out.preferred_height,
                                   out.horizontal ? preferred_cross : preferred_primary);
  }
  if (!std::isfinite(out.min_width) || !std::isfinite(out.min_height) ||
      !std::isfinite(out.preferred_width) || !std::isfinite(out.preferred_height)) {
    return InvalidPrepared("layout dimensions exceed the supported numeric range");
  }
  // Required bounds are exact. In particular, allowing a tiny max < min would
  // violate std::clamp's ordered-bound precondition in the following pass.
  if (Maximum(spec.max_width) < out.min_width ||
      Maximum(spec.max_height) < out.min_height) {
    return InvalidPrepared("contradictory required layout bounds");
  }
  out.preferred_width = std::clamp(out.preferred_width, out.min_width,
                                  Maximum(spec.max_width));
  out.preferred_height = std::clamp(out.preferred_height, out.min_height,
                                   Maximum(spec.max_height));
  return out;
}

std::vector<double> Distribute(PreparedNode const& node, double available) {
  std::vector<double> sizes;
  double total = 0;
  for (auto const& child : node.children) {
    double const preferred = node.horizontal ? child.preferred_width
                                             : child.preferred_height;
    sizes.push_back(preferred);
    total += preferred;
  }
  if (total > available) {
    // All preferred-size constraints have the same soft priority. Consume the
    // available compressible room proportionally; required minimums never move.
    double room = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
      auto const& child = node.children[i];
      room += sizes[i] - (node.horizontal ? child.min_width : child.min_height);
    }
    double const reduction = std::min(total - available, room);
    if (room > kEpsilon) {
      for (std::size_t i = 0; i < sizes.size(); ++i) {
        auto const& child = node.children[i];
        double const minimum = node.horizontal ? child.min_width : child.min_height;
        sizes[i] -= reduction * (sizes[i] - minimum) / room;
      }
    }
  } else {
    double spare = available - total;
    // Water-fill growth: a child reaching its maximum gives the remaining space
    // back to its growing siblings. At most one child saturates per pass.
    for (std::size_t pass = 0; pass < sizes.size() && spare > kEpsilon; ++pass) {
      double weight = 0;
      for (std::size_t i = 0; i < sizes.size(); ++i) {
        auto const& child = node.children[i];
        double const maximum = Maximum(node.horizontal ? child.spec->max_width
                                                       : child.spec->max_height);
        if (maximum - sizes[i] > kEpsilon) weight += child.spec->grow;
      }
      if (weight <= kEpsilon) break;
      double consumed = 0;
      for (std::size_t i = 0; i < sizes.size(); ++i) {
        auto const& child = node.children[i];
        double const maximum = Maximum(node.horizontal ? child.spec->max_width
                                                       : child.spec->max_height);
        double const addition = std::min(maximum - sizes[i],
                                        spare * child.spec->grow / weight);
        sizes[i] += addition;
        consumed += addition;
      }
      spare -= consumed;
      if (consumed <= kEpsilon) break;
    }
  }
  return sizes;
}

void Arrange(PreparedNode const& node, double x, double y, double width,
             double height, UiConstraintLayoutResult& result) {
  if (node.spec->flow == UiLayoutFlow::kLeaf) {
    result.controls.push_back({node.spec->control_id, x, y, width, height});
    result.content_width = std::max(result.content_width, x + width);
    result.content_height = std::max(result.content_height, y + height);
    return;
  }
  double const padding = node.spec->padding;
  double const primary = node.horizontal ? width : height;
  double const cross = std::max(0.0, (node.horizontal ? height : width) - 2 * padding);
  double const gaps = node.children.empty() ? 0
                          : node.spec->gap * (node.children.size() - 1);
  auto const sizes = Distribute(node, std::max(0.0, primary - 2 * padding - gaps));
  double offset = padding;
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    auto const& child = node.children[i];
    double const min_cross = node.horizontal ? child.min_height : child.min_width;
    double const preferred_cross = node.horizontal ? child.preferred_height
                                                   : child.preferred_width;
    double const max_cross = Maximum(node.horizontal ? child.spec->max_height
                                                     : child.spec->max_width);
    double const wanted_cross = node.spec->alignment == UiLayoutAlignment::kStretch
                                   ? cross : std::min(cross, preferred_cross);
    double const child_cross = std::clamp(wanted_cross, min_cross, max_cross);
    double cross_offset = padding;
    if (node.spec->alignment == UiLayoutAlignment::kCenter) {
      cross_offset += std::max(0.0, (cross - child_cross) / 2);
    } else if (node.spec->alignment == UiLayoutAlignment::kEnd) {
      cross_offset += std::max(0.0, cross - child_cross);
    }
    Arrange(child, x + (node.horizontal ? offset : cross_offset),
            y + (node.horizontal ? cross_offset : offset),
            node.horizontal ? sizes[i] : child_cross,
            node.horizontal ? child_cross : sizes[i], result);
    offset += sizes[i] + node.spec->gap;
  }
  result.content_width = std::max(result.content_width, x + width);
  result.content_height = std::max(result.content_height, y + height);
}

UiConstraintLayoutResult Compute(UiLayoutNode const& root, double width,
                                 double height, Measurements const& measurements) {
  std::unordered_set<std::uint32_t> ids;
  std::size_t count = 0;
  auto prepared = Prepare(root, width, height, measurements, ids, count, 0);
  if (!prepared.valid) return InvalidResult(std::move(prepared.error));
  double const content_width = std::clamp(std::max(width, prepared.min_width),
                                        prepared.min_width, Maximum(root.max_width));
  double const content_height = std::clamp(std::max(height, prepared.min_height),
                                         prepared.min_height, Maximum(root.max_height));
  UiConstraintLayoutResult result;
  Arrange(prepared, 0, 0, content_width, content_height, result);
  result.overflow_x = result.content_width > width + kEpsilon;
  result.overflow_y = result.content_height > height + kEpsilon;
  return result;
}

bool SameMeasurement(UiLayoutMeasurement const& left,
                     UiLayoutMeasurement const& right) {
  return std::abs(left.min_width - right.min_width) <= kEpsilon &&
         std::abs(left.min_height - right.min_height) <= kEpsilon &&
         std::abs(left.preferred_width - right.preferred_width) <= kEpsilon &&
         std::abs(left.preferred_height - right.preferred_height) <= kEpsilon;
}

}  // namespace

bool ValidateUiConstraintLayout(UiLayoutNode const& root, std::string* error) {
  // Zero intrinsic measurements isolate validity of the stored description.
  // The runtime additionally checks compatibility with actual native minimums.
  Measurements measurements;
  std::vector<UiLayoutNode const*> pending{&root};
  std::size_t count = 0;
  while (!pending.empty()) {
    auto const* node = pending.back();
    pending.pop_back();
    if (++count > kMaxNodes) {
      if (error) *error = "layout tree exceeds supported complexity";
      return false;
    }
    if (node->flow == UiLayoutFlow::kLeaf) {
      measurements.emplace(node->control_id,
                           UiLayoutMeasurement{node->control_id});
    }
    for (auto const& child : node->children) pending.push_back(&child);
  }
  std::unordered_set<std::uint32_t> ids;
  count = 0;
  auto prepared = Prepare(root, 0, 0, measurements, ids, count, 0);
  // A stored adaptive definition must be valid in both of its possible flows;
  // accepting only the initial horizontal flow could make an ordinary portrait
  // resize fail after a successfully committed model Event.
  if (prepared.valid && root.flow == UiLayoutFlow::kAdaptive) {
    ids.clear();
    count = 0;
    prepared = Prepare(root, 0, 1, measurements, ids, count, 0);
  }
  if (error) *error = std::move(prepared.error);
  return prepared.valid;
}

UiConstraintLayoutResult ComputeUiConstraintLayout(
    UiLayoutNode const& root, double viewport_width, double viewport_height,
    std::vector<UiLayoutMeasurement> const& measurements,
    UiLayoutRemeasure const& remeasure) {
  if (!IsNonnegative(viewport_width) || !IsNonnegative(viewport_height)) {
    return InvalidResult("layout requires a finite nonnegative viewport");
  }
  Measurements local;
  for (auto const& value : measurements) {
    if (!ValidateMeasurement(value)) {
      return InvalidResult("invalid layout measurement");
    }
    if (!local.emplace(value.control_id, value).second) {
      return InvalidResult("duplicate layout measurement");
    }
  }
  for (int pass = 0; pass < kMaxMeasurePasses; ++pass) {
    auto result = Compute(root, viewport_width, viewport_height, local);
    if (!result.valid || !remeasure) return result;
    bool changed = false;
    for (auto const& rect : result.controls) {
      auto value = remeasure(rect.control_id, rect.width);
      if (!ValidateMeasurement(value) || value.control_id != rect.control_id) {
        return InvalidResult("remeasure returned an invalid control measurement");
      }
      auto& old = local.at(rect.control_id);
      changed |= !SameMeasurement(old, value);
      old = value;
    }
    if (!changed) return result;
  }
  return InvalidResult("layout text measurement did not converge");
}

}  // namespace apptraverse::ui
