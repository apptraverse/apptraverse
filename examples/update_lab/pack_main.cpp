#include "model.h"
#include "apptraverse/directory_domain_storage.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
using namespace update_lab;
namespace {
Bytes Read(fs::path const& p) { Bytes b; if (!ReadFile(p, b)) Fail("cannot read " + p.string()); return b; }
void Write(fs::path const& p, Bytes const& b) { if (b.empty() || !AtomicWrite(p, b)) Fail("cannot write " + p.string()); }
void Emit(std::ostream& out, char const* name, Bytes const& b) {
  out << "inline constexpr unsigned char " << name << "[] = {";
  for (auto v : b) out << static_cast<unsigned>(v) << ',';
  out << "};\n";
}
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
      !apptraverse::DeserializeUserStorage(changed, delta.scope_metadata)) Fail("delta unpack failed");
  changed.state = std::move(delta.changed);
  Layers(dir, changed);
  // Scope replacement and removals also describe transitions with no changed layer.
  delta.changed.clear();
  Write(dir / "operations.bin", Encode(delta));
}

}
int main(int argc, char** argv) {
  RegisterModel();
  if (argc < 2) Fail("usage: update_lab_pack seed|publish|full|delta|apply|inspect ...");
  std::string action = argv[1];
  if (action == "seed" && argc == 3) {
    std::error_code ec; fs::create_directories(fs::path{argv[2]}.parent_path(), ec);
    std::ofstream out{argv[2]};
    out << "#pragma once\nnamespace update_lab {\n";
    auto seed = DistillRelease(0);
    Emit(out, "kSeedSupply", FullState(seed.embedded, 0));
    Emit(out, "kSeedRelease", Encode(seed.definition));
    out << "}\n";
    return out ? 0 : 1;
  }
  if (action == "full" && argc == 5) {
    ae::RamDomainStorage state;
    if (!apptraverse::DeserializeUserStorage(state, Read(argv[2]))) Fail("native state read failed");
    Write(argv[4], FullState(state, static_cast<unsigned>(std::stoul(argv[3]))));
    return 0;
  }
  if (action == "delta" && argc == 7) {
    ae::RamDomainStorage before, after;
    if (!apptraverse::DeserializeUserStorage(before, Read(argv[2])) ||
        !apptraverse::DeserializeUserStorage(after, Read(argv[3]))) Fail("native state read failed");
    Write(argv[6], DeltaState(before, after, static_cast<unsigned>(std::stoul(argv[4])), static_cast<unsigned>(std::stoul(argv[5]))));
    return 0;
  }
  if (action == "apply" && argc == 6) {
    ae::RamDomainStorage state;
    unsigned version = static_cast<unsigned>(std::stoul(argv[4]));
    if (!apptraverse::DeserializeUserStorage(state, Read(argv[2])) ||
        !ApplyStatePackage(Read(argv[3]), state, version)) Fail("delta source version does not match");
    Bytes b; apptraverse::SerializeUserStorage(state, b); Write(argv[5], b);
    std::cout << "{\"version\":" << version << "}\n";
    return 0;
  }
  if (action == "inspect" && argc == 3) {
    StatePackage p; Bytes raw;
    if (!UnpackRaw(Read(argv[2]), p, raw)) Fail("package read failed");
    std::cout << "{\"kind\":" << p.kind << ",\"from\":" << p.from << ",\"to\":" << p.to
              << ",\"raw_bytes\":" << p.raw_size << ",\"compressed_bytes\":" << p.compressed.size() << "}\n";
    return 0;
  }
  if (action == "corpus" && argc == 3) {
    auto storage = BuildSupply(3);
    ae::Domain domain{storage};
    std::vector<TextObject::ptr> keep;
    for (unsigned n = 0; n < 20000; ++n) {
      auto object = TextObject::ptr::Create(ae::CreateWith{domain}.with_id(ae::ObjId{0x7c000000U + n}));
      object->SetOriginId(kPrefabId);
      object->name = "User label " + std::to_string(n) + ": " +
          (n % 3 == 0 ? "Локализованный текст" : n % 3 == 1 ? "Localized text" : "Lokalisierter Text");
      object->badge = "S1 / prefab R3";
      object->owns_badge = false;
      object.Save(); keep.push_back(object);
    }
    Bytes raw; apptraverse::SerializeUserStorage(storage, raw); Write(argv[2], raw);
    std::cout << "{\"objects\":20000,\"bytes\":" << raw.size() << "}\n";
    return 0;
  }
  if (action != "publish") Fail("incorrect command arguments");
  std::map<std::string, std::string> options;
  for (int i = 2; i + 1 < argc; i += 2) options[argv[i]] = argv[i+1];
  fs::path store = options.at("--out");
  FileLock lock{store / "publisher.lock"};
  if (!lock.acquired()) Fail("publisher already running");
  Catalog catalog{2, 0, {}, {}};
  Catalog published{};
  if (fs::exists(store / "catalog.bin")) {
    if (!ReadCatalog(Read(store / "catalog.bin"), published)) Fail("incompatible store; choose a new output directory");
    catalog.components = published.components;
    catalog.platforms = published.platforms;
  }
  auto immutable = [&](fs::path const& path, Bytes const& bytes) {
    if (!fs::exists(path)) { Write(path, bytes); return; }
    if (Read(path) != bytes) Fail("published artifact differs: " + path.string() + "; use a new release or store");
  };
  auto record = [&](std::string const& id, fs::path const& path, Bytes const& bytes) {
    immutable(store / path, bytes);
    if (!FindComponent(catalog, id)) catalog.components.push_back({id, bytes.size(), path.generic_string()});
  };
  unsigned through = options.contains("--through") ? std::stoul(options.at("--through")) : 3;
  if (through > 3) Fail("fixture supplies R0 through R3");
  if (!published.releases.empty() && through < published.generation) Fail("cannot remove published releases");
  std::vector<DistilledRelease> releases;
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
      auto exe = Read(options.at(key));
      auto elf = std::string{platform} == "linux-x64";
      auto path = store / dir / platform / (elf ? "update_lab" : "update_lab.exe");
      immutable(path, exe);
#ifndef _WIN32
      if (elf) fs::permissions(path, fs::perms::owner_exec, fs::perm_options::add);
#endif
      record(ExeId(platform, r.executable), dir / platform / "update.ats", PackageRaw(exe, 2, r.executable, r.executable));
      auto launcher = options.find("--" + std::string{platform} + "-launcher");
      if (launcher != options.end()) {
        auto entry = path.parent_path() / (elf ? "update_lab_launcher" : "update_lab_launcher.exe");
        immutable(entry, Read(launcher->second));
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
                                    PackageRaw(Read(options.at(key)), 2, 2, 2));
  }
  catalog.generation = through;
  Write(store / "catalog.bin", Encode(catalog));
  std::cout << "Published " << releases.size() << " releases, " << catalog.components.size() << " components\n";
}
