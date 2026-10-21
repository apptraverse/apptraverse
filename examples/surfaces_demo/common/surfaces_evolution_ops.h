#pragma once

#include <filesystem>
#include <string>

namespace apptraverse {

int RunSurfacesEvolutionOp(std::filesystem::path const& state_dir,
                           std::string const& operation);

}  // namespace apptraverse
