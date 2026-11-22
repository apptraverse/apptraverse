#include "model.h"
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
}
int main(int argc, char** argv) {
  RegisterModel();
  if (argc < 2) Fail("usage: update_lab_pack seed|publish|full|delta|apply|inspect ...");
  std::string action = argv[1];
  if (action == "seed" && argc == 3) {
    std::error_code ec; fs::create_directories(fs::path{argv[2]}.parent_path(), ec);
    std::ofstream out{argv[2]};
    out << "#pragma once\nnamespace update_lab {\n";
    Emit(out, "kSeedSupply", FullState(BuildSupply(0), 0));
    Emit(out, "kSeedLanguage", BuildLanguageDelivery(0, "en"));
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
  Catalog catalog{1, 1, {}, {}};
  auto add = [&](std::string id, Bytes bytes) {
    Write(store / "components" / FileId(id), bytes);
    catalog.components.push_back({std::move(id), bytes.size()});
  };
  std::vector<ae::RamDomainStorage> images(4);
  for (unsigned n = 0; n < 4; ++n) {
    auto core = BuildSupply(n);
    auto image = BuildCompleteSupply(n);
    images[n].state = image.state; images[n].object_scopes = image.object_scopes;
    auto full = FullState(image, n);
    Write(store / "images" / ("R" + std::to_string(n) + ".full.ats"), full);
    Bytes raw; apptraverse::SerializeUserStorage(image, raw);
    Write(store / "images" / ("R" + std::to_string(n) + ".native.bin"), raw);
    std::string supply = "supply/" + std::to_string(n);
    add(supply, FullState(core, n));
    auto langs = n < 2 ? std::vector<std::string>{"en", "ru"} : std::vector<std::string>{"en", "ru", "de"};
    catalog.releases.push_back({n, n == 3 ? 1U : 0U, supply, langs});
    for (auto const& l : langs) add(LanguageId(n, l), BuildLanguageDelivery(n, l));
    if (n) {
      auto delta = DeltaState(images[n-1], images[n], n-1, n);
      Write(store / "images" / ("R" + std::to_string(n-1) + "-R" + std::to_string(n) + ".delta.ats"), delta);
      auto previous_core = BuildSupply(n-1);
      add("delta/" + std::to_string(n-1) + "/" + std::to_string(n), DeltaState(previous_core, core, n-1, n));
    }
  }
  for (auto platform : {"windows-x64", "linux-x64"}) for (unsigned e = 0; e < 3; ++e) {
    std::string key = "--" + std::string{platform} + "-e" + std::to_string(e);
    if (options.contains(key)) add("executable/" + std::string{platform} + "/" + std::to_string(e),
        PackageRaw(Read(options[key]), 2, e, e));
  }
  Write(store / "catalog.bin", Encode(catalog));
  std::cout << "{\"releases\":4,\"components\":" << catalog.components.size() << "}\n";
}
