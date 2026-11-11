#pragma once
#include <filesystem>
namespace apptraverse::example::minimal {
// The process registers its application types before selecting this Debug mode.
std::filesystem::path DistillMinimalApplication(std::filesystem::path const& output_directory);
}
