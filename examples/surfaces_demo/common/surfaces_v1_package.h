#pragma once

#include <filesystem>

namespace apptraverse {

std::filesystem::path SurfacesDemoBundledV1PackageDirectory();

// Copy packaged v1 layer files into user state without touching existing v1 or v0.
void SurfacesDemoInstallBundledV1Layers(
    std::filesystem::path const& state_dir,
    std::filesystem::path const& package_dir);

// After user state exists, write missing Surface v1 layers (default suffix per number).
void SurfacesDemoEnsureMissingSurfaceV1Layers(
    std::filesystem::path const& state_dir);

}  // namespace apptraverse
