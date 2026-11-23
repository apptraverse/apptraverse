#include "model.h"
#include "publish.h"
#include <fstream>
#include <iostream>
using namespace update_lab;
namespace {
void Emit(std::ostream& out, char const* name, Bytes const& b) {
  out << "inline constexpr unsigned char " << name << "[] = {";
  for (auto v : b) out << static_cast<unsigned>(v) << ',';
  out << "};\n";
}
}
int main(int argc, char** argv) {
  if (argc < 2) Fail("usage: update_lab_pack seed|publish|full|delta|apply|inspect ...");
  std::string action = argv[1];
  if (action == "publish") return PublishStore(argc, argv);
  RegisterModel();
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
    if (!apptraverse::DeserializeUserStorage(state, ReadInput(argv[2]))) Fail("native state read failed");
    WriteOutput(argv[4], FullState(state, static_cast<unsigned>(std::stoul(argv[3]))));
    return 0;
  }
  if (action == "delta" && argc == 7) {
    ae::RamDomainStorage before, after;
    if (!apptraverse::DeserializeUserStorage(before, ReadInput(argv[2])) ||
        !apptraverse::DeserializeUserStorage(after, ReadInput(argv[3]))) Fail("native state read failed");
    WriteOutput(argv[6], DeltaState(before, after, static_cast<unsigned>(std::stoul(argv[4])), static_cast<unsigned>(std::stoul(argv[5]))));
    return 0;
  }
  if (action == "apply" && argc == 6) {
    ae::RamDomainStorage state;
    unsigned version = static_cast<unsigned>(std::stoul(argv[4]));
    if (!apptraverse::DeserializeUserStorage(state, ReadInput(argv[2])) ||
        !ApplyStatePackage(ReadInput(argv[3]), state, version)) Fail("delta source version does not match");
    Bytes b; apptraverse::SerializeUserStorage(state, b); WriteOutput(argv[5], b);
    std::cout << "{\"version\":" << version << "}\n";
    return 0;
  }
  if (action == "inspect" && argc == 3) {
    StatePackage p; Bytes raw;
    if (!UnpackRaw(ReadInput(argv[2]), p, raw)) Fail("package read failed");
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
    Bytes raw; apptraverse::SerializeUserStorage(storage, raw); WriteOutput(argv[2], raw);
    std::cout << "{\"objects\":20000,\"bytes\":" << raw.size() << "}\n";
    return 0;
  }
  Fail("incorrect command arguments");
}
