#pragma once
// Explicit headless diagnostic export. Product runtime never writes these
// files.
#include <filesystem>
#include <fstream>
#include <string>
namespace apptraverse {
struct TelemetryScenarioOptions {
  std::filesystem::path output;
  std::filesystem::path persistent_profile;
  bool model_only{};
};
inline bool ParseTelemetryScenarioOptions(int argc, char** argv,
                                          TelemetryScenarioOptions& options) {
  if (argc < 2) return false;
  options.output = argv[1];
  for (int index = 2; index < argc; ++index) {
    std::string argument = argv[index];
    if (argument == "--model-only")
      options.model_only = true;
    else if (argument == "--persistent-profile" && index + 1 < argc)
      options.persistent_profile = argv[++index];
    else
      return false;
  }
  return !options.model_only || options.persistent_profile.empty();
}
template <class Sink>
bool ExportTelemetryScenario(std::filesystem::path const& output,
                             std::string const& build) {
  auto& sink = Sink::Instance();
  if (!sink.DrainAll()) return false;
  std::error_code error;
  std::filesystem::create_directories(output, error);
  if (error) return false;
  auto const stem = std::string{Sink::kName};
  auto bytes = sink.GetBackend()->Snapshot(Sink::kName);
  std::ofstream raw{output / (stem + ".raw"), std::ios::binary};
  raw.write(reinterpret_cast<char const*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  std::ofstream map{output / (stem + ".observed-map.json")};
  return bool(raw) && sink.ExportMap(map, build);
}
}  // namespace apptraverse
