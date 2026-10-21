#include "apptraverse/surfaces/surfaces_v1_hooks.h"

#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {

namespace {

std::string DefaultTitleSuffixForSurfaceNumber(std::uint32_t number) {
  return " [s" + std::to_string(number) + "]";
}

}  // namespace

void SurfacesDemoOnSurfaceAdded(Surface& surface) {
  surface.window_title_suffix = DefaultTitleSuffixForSurfaceNumber(surface.number);
}

}  // namespace apptraverse
