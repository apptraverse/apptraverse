#include "publish.h"
#include "model.h"
#include "apptraverse/directory_domain_storage.h"
#include <algorithm>
#include <iostream>

namespace update_lab {
Bytes ReadInput(fs::path const& p) { Bytes b; if (!ReadFile(p, b)) Fail("cannot read " + p.string()); return b; }
void WriteOutput(fs::path const& p, Bytes const& b) { if (b.empty() || !AtomicWrite(p, b)) Fail("cannot write " + p.string()); }
namespace {
void Layers(fs::path const& dir, ae::RamDomainStorage const& state) {
  apptraverse::DirectoryDomainStorage disk{dir};
  for (auto const& [id, classes] : state.state) {
    auto object = dir / std::to_string(id.id());
    fs::create_directories(object);
    if (!classes) {
      if (!AtomicWrite(object / ".removed", {})) Fail("cannot write tombstone");
      continue;
    }
    for (auto const& [cls, versions] : *classes) {
      fs::create_directories(object / std::to_string(cls));
      for (auto const& [v, bytes] : versions)
        if (!AtomicWrite(apptraverse::DirectoryDomainStorageLayerPath(dir, {id, cls, v}), bytes))
          Fail("cannot write layer");
    }
  }
  for (auto const& [id, scope] : state.object_scopes)
    if (!disk.CommitObjectScope(id, scope, true)) Fail("cannot write object scope");
}
void DeltaLayers(fs::path const& dir, Bytes const& bytes) {
  StatePackage package; StateDelta delta; Bytes raw;
  ae::RamDomainStorage changed;
  if (!UnpackRaw(bytes, package, raw) || !Decode(raw, delta) ||
      !apptraverse::DeserializeNativeStorage(changed, delta.scope_metadata)) Fail("delta unpack failed");
  changed.state = std::move(delta.changed);
  Layers(dir, changed);
  // Scope replacement and removals also describe transitions with no changed layer.
  delta.changed.clear();
  WriteOutput(dir / "operations.bin", Encode(delta));
}

}
int PublishStore(int argc, char** argv) {
  RegisterModel();
  std::map<std::string, std::string> options;
  if ((argc - 2) % 2 != 0) Fail("publish options require a value");
  for (int i = 2; i < argc; i += 2) options[argv[i]] = argv[i+1];
  if (!options.contains("--out")) Fail("publish requires --out");
  fs::path store = options.at("--out");
  FileLock lock{store / "publisher.lock"};
  if (!lock.acquired()) Fail("publisher already running");
  Catalog catalog{kCatalogFormat, 0, {}, {}};
  Catalog published{};
  if (fs::exists(store / "catalog.bin")) {
    if (!ReadCatalog(ReadInput(store / "catalog.bin"), published)) Fail("incompatible store; choose a new output directory");
    catalog.components = published.components;
    catalog.platforms = published.platforms;
  }
  auto immutable = [&](fs::path const& path, Bytes const& bytes) {
    if (!fs::exists(path)) { WriteOutput(path, bytes); return; }
    if (ReadInput(path) != bytes) Fail("published artifact differs: " + path.string() + "; use a new release or store");
  };
  auto record = [&](std::string const& id, fs::path const& path, Bytes const& bytes) {
    immutable(store / path, bytes);
    if (!FindComponent(catalog, id)) catalog.components.push_back({id, bytes.size(), path.generic_string()});
  };
  unsigned through = options.contains("--through") ? std::stoul(options.at("--through")) : 3;
  if (through > 3) Fail("fixture supplies R0 through R3");
  if (!published.releases.empty() && through < published.generation) Fail("cannot remove published releases");
  std::vector<DistilledRelease> releases;
  // A single E may appear in multiple complete release images.
  std::map<std::string, Bytes> encoded_executables;
  for (unsigned n = 0; n <= through; ++n) {
    auto dir = fs::path{"images"} / ("R" + std::to_string(n));
    auto current = DistillRelease(n);
    auto& r = current.definition;
    auto previous = releases.empty() ? nullptr : &releases.back();
    record(r.supply, dir / "full.ats", FullState(current.embedded, n));
    Layers(store / dir / "full", current.embedded);
    for (auto& lang : r.languages) {
      if (lang.full.empty()) continue;
      auto const& graph = current.downloads.at(lang.locale);
      auto old = previous ? FindLanguage(previous->definition, lang.locale) : nullptr;
      if (old && !old->full.empty()) {
        auto const& before = previous->downloads.at(lang.locale);
        if (before.state == graph.state && before.object_scopes == graph.object_scopes) {
          lang.revision = old->revision; lang.full = old->full;
        }
      }
      record(lang.full, dir / "downloads" / (lang.locale + ".ats"), FullState(graph, lang.revision));
    }
    for (auto platform : {"windows-x64", "linux-x64"}) {
      auto key = "--" + std::string{platform} + "-e" + std::to_string(r.executable);
      if (!options.contains(key)) continue;
      auto& supported = catalog.platforms[platform];
      if (std::find(supported.begin(), supported.end(), n) == supported.end()) supported.push_back(n);
      auto exe = ReadInput(options.at(key));
      auto elf = std::string{platform} == "linux-x64";
      auto path = store / dir / platform / (elf ? "update_lab" : "update_lab.exe");
      immutable(path, exe);
#ifndef _WIN32
      if (elf) fs::permissions(path, fs::perms::owner_exec, fs::perm_options::add);
#endif
      auto id = ExeId(platform, r.executable);
      auto [packed, inserted] = encoded_executables.try_emplace(id);
      if (inserted) packed->second = PackageRaw(exe, 2, r.executable, r.executable);
      record(id, dir / platform / "update.ats", packed->second);
      auto launcher = options.find("--" + std::string{platform} + "-launcher");
      if (launcher != options.end()) {
        auto entry = path.parent_path() / (elf ? "update_lab_launcher" : "update_lab_launcher.exe");
        immutable(entry, ReadInput(launcher->second));
#ifndef _WIN32
        if (elf) fs::permissions(entry, fs::perms::owner_exec, fs::perm_options::add);
#endif
      }
      // Accept matching producer artifacts, including telemetry decoder dictionaries and symbols.
      for (auto suffix : {"-telemetry", "-symbols"}) {
        if (!options.contains(key + suffix)) continue;
        fs::path source = options.at(key + suffix);
        auto dest = store / dir / platform / "telemetry";
        fs::create_directories(dest);
        fs::copy(source, dest / source.filename(), fs::copy_options::recursive | fs::copy_options::overwrite_existing);
      }
    }
    immutable(store / dir / "release.bin", Encode(r));
    catalog.releases.push_back(r);
    releases.push_back(std::move(current));
  }
  for (auto const& src : releases) for (auto const& dst : releases) {
    auto from = src.definition.id, to = dst.definition.id;
    if (from + 1 != to && to + 1 != from) continue;
    auto dir = fs::path{"images"} / ("R" + std::to_string(to)) / "deltas" / ("from-R" + std::to_string(from));
    auto base = DeltaState(src.embedded, dst.embedded, from, to);
    record(DeltaId(from, to), dir / "state.ats", base);
    DeltaLayers(store / dir / "state", base);
    for (auto const& lang : dst.definition.languages) {
      auto old = FindLanguage(src.definition, lang.locale);
      if (!old || old->full.empty() || lang.full.empty() || lang.revision == old->revision) continue;
      auto delta = DeltaState(src.downloads.at(lang.locale), dst.downloads.at(lang.locale), old->revision, lang.revision);
      record(DeltaId(old->revision, lang.revision, lang.locale), dir / "downloads" / (lang.locale + ".ats"), delta);
    }
  }
  for (auto platform : {"windows-x64", "linux-x64"}) {
    auto key = "--" + std::string{platform} + "-e2";
    if (options.contains(key)) record(ExeId(platform, 2), fs::path{"fixtures"} / platform / "broken.ats",
                                    PackageRaw(ReadInput(options.at(key)), 2, 2, 2));
  }
  catalog.generation = through;
  WriteOutput(store / "catalog.bin", Encode(catalog));
  std::cout << "Published " << releases.size() << " releases, " << catalog.components.size() << " components\n";
  return 0;
}
}
