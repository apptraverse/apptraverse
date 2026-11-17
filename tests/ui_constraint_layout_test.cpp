#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "apptraverse/ui/ui_constraint_layout.h"

namespace {
using namespace apptraverse::ui;

#define CHECK(condition)                                                     \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << "CHECK failed: " #condition << " at line " << __LINE__   \
                << '\n';                                                   \
      std::exit(1);                                                        \
    }                                                                      \
  } while (false)

bool Near(double left, double right) { return std::abs(left - right) < 0.00001; }

UiLayoutNode Leaf(std::uint32_t id, double grow = 0) {
  UiLayoutNode leaf;
  leaf.control_id = id;
  leaf.grow = grow;
  return leaf;
}

UiLayoutNode Row() {
  UiLayoutNode root;
  root.flow = UiLayoutFlow::kHorizontal;
  root.padding = 10;
  root.gap = 5;
  root.children = {Leaf(1, 1), Leaf(2, 3)};
  return root;
}

std::vector<UiLayoutMeasurement> PairMeasurements() {
  return {{1, 40, 20, 80, 30}, {2, 60, 20, 100, 30}};
}

void TestGrowthCompressionAndOverflow() {
  auto root = Row();
  auto large = ComputeUiConstraintLayout(root, 400, 80, PairMeasurements());
  CHECK(large.valid);
  CHECK(large.controls.size() == 2);
  CHECK(Near(large.controls[0].width, 128.75));
  CHECK(Near(large.controls[1].width, 246.25));
  CHECK(Near(large.controls[0].height, 60));
  CHECK(Near(large.controls[1].x,
             large.controls[0].x + large.controls[0].width + 5));
  CHECK(!large.overflow_x && !large.overflow_y);

  auto small = ComputeUiConstraintLayout(root, 150, 80, PairMeasurements());
  CHECK(small.valid);
  CHECK(Near(small.controls[0].width, 52.5));
  CHECK(Near(small.controls[1].width, 72.5));
  CHECK(!small.overflow_x);

  auto tiny = ComputeUiConstraintLayout(root, 90, 30, PairMeasurements());
  CHECK(tiny.valid);
  CHECK(tiny.overflow_x && tiny.overflow_y);
  CHECK(Near(tiny.content_width, 125));
  CHECK(Near(tiny.content_height, 40));
  CHECK(Near(tiny.controls[0].width, 40));
  CHECK(Near(tiny.controls[1].width, 60));
  CHECK(Near(tiny.controls[0].height, 20));
}

void TestMaximaAndAlignment() {
  auto root = Row();
  root.padding = root.gap = 0;
  root.alignment = UiLayoutAlignment::kCenter;
  root.children[0].max_width = 90;
  auto result = ComputeUiConstraintLayout(root, 500, 100, PairMeasurements());
  CHECK(result.valid);
  CHECK(Near(result.controls[0].width, 90));
  CHECK(Near(result.controls[1].width, 410));
  CHECK(Near(result.controls[0].y, 35));
  CHECK(Near(result.controls[0].height, 30));

  root.alignment = UiLayoutAlignment::kEnd;
  result = ComputeUiConstraintLayout(root, 500, 100, PairMeasurements());
  CHECK(result.valid);
  CHECK(Near(result.controls[0].y, 70));
}

void TestAdaptiveNestedContainersAndRuntimeChanges() {
  UiLayoutNode root;
  root.flow = UiLayoutFlow::kAdaptive;
  root.padding = 12;
  root.gap = 8;
  auto content = Leaf(1, 1);
  content.min_width = 100;
  content.min_height = 80;
  UiLayoutNode actions;
  actions.flow = UiLayoutFlow::kVertical;
  actions.gap = 8;
  actions.children = {Leaf(2), Leaf(3)};
  root.children = {content, actions};
  std::vector<UiLayoutMeasurement> measurements{
      {1, 100, 80, 180, 100}, {2, 70, 30, 110, 30}, {3, 90, 30, 130, 30}};
  auto wide = ComputeUiConstraintLayout(root, 600, 300, measurements);
  CHECK(wide.valid);
  CHECK(wide.controls.size() == 3);
  CHECK(wide.controls[1].x > wide.controls[0].x);
  CHECK(Near(wide.controls[1].width, wide.controls[2].width));
  CHECK(Near(wide.controls[1].width, 130));
  CHECK(wide.controls[2].y > wide.controls[1].y);

  auto tall = ComputeUiConstraintLayout(root, 300, 600, measurements);
  CHECK(tall.valid);
  CHECK(tall.controls[1].y >= tall.controls[0].y + tall.controls[0].height + 8);
  CHECK(Near(tall.controls[1].x, tall.controls[0].x));

  // An Event or a newer distilled definition supplies another value tree. The
  // same engine handles it without a platform-specific placement calculation.
  root.flow = UiLayoutFlow::kVertical;
  root.gap = 21;
  auto changed = ComputeUiConstraintLayout(root, 600, 300, measurements);
  CHECK(changed.valid);
  CHECK(changed.controls[1].y >=
        changed.controls[0].y + changed.controls[0].height + 21 - 0.00001);
  CHECK(Near(root.gap, 21));
}

void TestNativeMetricsAndWidthDependentMeasurement() {
  UiLayoutNode root;
  root.flow = UiLayoutFlow::kVertical;
  root.gap = 5;
  root.children = {Leaf(1), Leaf(2, 1)};
  std::vector<UiLayoutMeasurement> measurements{
      {1, 40, 20, 200, 20}, {2, 20, 20, 20, 20}};
  int calls = 0;
  auto wrapped = ComputeUiConstraintLayout(
      root, 100, 200, measurements,
      [&calls](std::uint32_t id, double width) {
        ++calls;
        if (id == 1) {
          double const height = width < 150 ? 60 : 20;
          return UiLayoutMeasurement{1, 40, height, 200, height};
        }
        return UiLayoutMeasurement{2, 20, 20, 20, 20};
      });
  CHECK(wrapped.valid);
  CHECK(calls == 4);
  CHECK(Near(wrapped.controls[0].height, 60));
  CHECK(Near(wrapped.controls[1].y, 65));
  CHECK(Near(wrapped.controls[1].height, 135));

  measurements[0].min_height = measurements[0].preferred_height = 100;
  auto large_text = ComputeUiConstraintLayout(root, 100, 200, measurements);
  CHECK(large_text.valid);
  CHECK(Near(large_text.controls[0].height, 100));
  CHECK(Near(large_text.controls[1].y, 105));
}

void TestInvalidDefinitionsAndMeasurements() {
  auto root = Row();
  std::string error;
  CHECK(ValidateUiConstraintLayout(root, &error));
  CHECK(error.empty());
  root.gap = -1;
  CHECK(!ValidateUiConstraintLayout(root, &error));
  CHECK(!error.empty());
  CHECK(!ComputeUiConstraintLayout(root, 400, 80, PairMeasurements()).valid);
  root = Row();
  root.children[1].control_id = 1;
  CHECK(!ValidateUiConstraintLayout(root));
  root = Row();
  root.children[0].max_width = 10;
  root.children[0].min_width = 20;
  CHECK(!ValidateUiConstraintLayout(root));
  root.children[0].max_width = 20 - 0.00000001;
  CHECK(!ValidateUiConstraintLayout(root));
  root = Row();
  root.children[0].max_width = 10;
  CHECK(ValidateUiConstraintLayout(root));
  CHECK(!ComputeUiConstraintLayout(root, 400, 80, PairMeasurements()).valid);
  root = Row();
  root.padding = std::numeric_limits<double>::quiet_NaN();
  CHECK(!ValidateUiConstraintLayout(root));
  root = Row();
  root.padding = std::numeric_limits<double>::max();
  CHECK(!ValidateUiConstraintLayout(root));
  root = Row();
  root.children[0].flow = UiLayoutFlow::kAdaptive;
  root.children[0].control_id = 0;
  root.children[0].children = {Leaf(3)};
  CHECK(!ValidateUiConstraintLayout(root));
  root = Row();
  root.flow = UiLayoutFlow::kAdaptive;
  root.padding = root.gap = 0;
  root.max_height = 80;
  root.children[0].min_height = root.children[1].min_height = 60;
  // Horizontal minimum height is 60, but vertical minimum is 120. Reject before
  // a definition/Event is accepted rather than failing after a portrait resize.
  CHECK(!ValidateUiConstraintLayout(root));
  root = Row();
  CHECK(!ComputeUiConstraintLayout(root, 400, 80, {}).valid);
  auto duplicate = PairMeasurements();
  duplicate.push_back(duplicate.front());
  CHECK(!ComputeUiConstraintLayout(root, 400, 80, duplicate).valid);
  CHECK(!ComputeUiConstraintLayout(root, -1, 80, PairMeasurements()).valid);
  auto invalid_callback = ComputeUiConstraintLayout(
      root, 400, 80, PairMeasurements(),
      [](std::uint32_t, double) { return UiLayoutMeasurement{}; });
  CHECK(!invalid_callback.valid);
}

void TestEmptyContainerAndLeafRoot() {
  UiLayoutNode empty;
  empty.flow = UiLayoutFlow::kVertical;
  empty.padding = 10;
  auto result = ComputeUiConstraintLayout(empty, 100, 80, {});
  CHECK(result.valid);
  CHECK(result.controls.empty());
  CHECK(Near(result.content_width, 100));
  CHECK(Near(result.content_height, 80));
  auto leaf = Leaf(1);
  result = ComputeUiConstraintLayout(leaf, 100, 80, {{1, 20, 20, 50, 30}});
  CHECK(result.valid);
  CHECK(result.controls.size() == 1);
  CHECK(Near(result.controls[0].width, 100));
}
}  // namespace

int main() {
  TestGrowthCompressionAndOverflow();
  TestMaximaAndAlignment();
  TestAdaptiveNestedContainersAndRuntimeChanges();
  TestNativeMetricsAndWidthDependentMeasurement();
  TestInvalidDefinitionsAndMeasurements();
  TestEmptyContainerAndLeafRoot();
  std::cout << "ui_constraint_layout_test PASS\n";
}
