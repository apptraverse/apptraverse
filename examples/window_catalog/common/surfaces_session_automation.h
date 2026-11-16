#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace apptraverse {

enum class SurfacesSessionAutomationOp {
  SetBounds,
  VerifyBounds,
  SetSuffix,
  VerifyTitleSuffix,
  Shutdown,
};

struct SurfacesSessionAutomationStep {
  SurfacesSessionAutomationOp op{};
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
  std::string text;
};

std::vector<SurfacesSessionAutomationStep> ParseSurfacesSessionAutomation(
    std::string const& spec);

int RunSurfacesSessionInspect(std::filesystem::path const& state_dir);

}  // namespace apptraverse
