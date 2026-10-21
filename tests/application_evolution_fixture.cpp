#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_macros.h"

// Six independent executables use this fixture. Only these test classes evolve;
// AppTraverse's working classes remain at schema zero.
#ifndef APPTRAVERSE_EXE_REVISION
#error APPTRAVERSE_EXE_REVISION must be set by the fixture target
#endif
#ifndef APPTRAVERSE_SCHEMA_REVISION
#error APPTRAVERSE_SCHEMA_REVISION must be set by the fixture target
#endif
#ifndef APPTRAVERSE_EVOLUTION_MODE
#error APPTRAVERSE_EVOLUTION_MODE must be set by the fixture target
#endif

namespace apptraverse::test::application_evolution {

static_assert(APPTRAVERSE_EXE_REVISION >= 0 && APPTRAVERSE_EXE_REVISION <= 2);
static_assert(APPTRAVERSE_SCHEMA_REVISION >= 0 &&
              APPTRAVERSE_SCHEMA_REVISION <= 2);
constexpr unsigned kBaseSchema = APPTRAVERSE_SCHEMA_REVISION >= 1 ? 1 : 0;
constexpr unsigned kDerivedSchema = APPTRAVERSE_SCHEMA_REVISION >= 2 ? 1 : 0;
constexpr char kExe = 'A' + APPTRAVERSE_EXE_REVISION;
constexpr std::uint32_t kRootId = 91001;
constexpr std::uint32_t kChildId = 91002;
constexpr std::uint32_t kStep[] = {1, 10, 100};

class StateRoot;

class LayerBase : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::application_evolution::LayerBase",
                          LayerBase, ae::Obj, kBaseSchema)

 protected:
  LayerBase() = default;

 public:
  explicit LayerBase(ae::ObjProp prop) : Obj{prop} {}

#if APPTRAVERSE_SCHEMA_REVISION >= 1
  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(added_base))
#else
  AE_OBJECT_REFLECT(AE_MMBR(value))
#endif

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
#if APPTRAVERSE_SCHEMA_REVISION >= 1
    added_base = 999;
#endif
  }

#if APPTRAVERSE_SCHEMA_REVISION >= 1
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, value, added_base);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, value, added_base);
  }
#else
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }
#endif

  std::uint32_t value{0};
#if APPTRAVERSE_SCHEMA_REVISION >= 1
  std::uint32_t added_base{999};
#endif
};

class LayerDerived : public LayerBase {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::application_evolution::LayerDerived", LayerDerived,
      LayerBase, kDerivedSchema)

 protected:
  LayerDerived() = default;

 public:
  explicit LayerDerived(ae::ObjProp prop) : LayerBase{prop} {}

#if APPTRAVERSE_SCHEMA_REVISION >= 2
  AE_OBJECT_REFLECT(AE_MMBR(child_value), AE_MMBR(label), AE_MMBR(backref),
                    AE_MMBR(added_derived))
#else
  AE_OBJECT_REFLECT(AE_MMBR(child_value), AE_MMBR(label), AE_MMBR(backref))
#endif

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, child_value, label, backref);
#if APPTRAVERSE_SCHEMA_REVISION >= 2
    added_derived = 777;
#endif
  }

#if APPTRAVERSE_SCHEMA_REVISION >= 2
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    // Keep the previous referent alive while a second stored schema reloads
    // the cyclic reference.
    auto const keep_backref_alive = backref;
    dnv(base_, child_value, label, backref, added_derived);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, child_value, label, backref, added_derived);
  }
#else
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, child_value, label, backref);
  }
#endif

  std::uint32_t child_value{0};
  std::string label;
  ae::ObjPtr<StateRoot> backref;
#if APPTRAVERSE_SCHEMA_REVISION >= 2
  std::uint32_t added_derived{777};
#endif
};

// Journals are exercised by the existing Node persistence tests. This fixture
// isolates schema dispatch, native class layers, identity, aliases and cycles.
class StateRoot : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::application_evolution::StateRoot",
                          StateRoot, ae::Obj, 0)

 protected:
  StateRoot() = default;

 public:
  explicit StateRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(child), AE_MMBR(alias_child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, child, alias_child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, child, alias_child);
  }

  LayerDerived::ptr child;
  LayerDerived::ptr alias_child;
};

APPTRAVERSE_REGISTER(LayerBase);
APPTRAVERSE_REGISTER(LayerDerived);
APPTRAVERSE_REGISTER(StateRoot);

std::uint32_t ParseNumber(std::string const& text) {
  std::uint32_t value{};
  auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    throw std::runtime_error("invalid numeric storage path: " + text);
  }
  return value;
}

// Test-local admission policy, not a modification of native serialization.
// Check every stored layer, including stale slots that a native old reader can
// silently ignore. This runs before constructing a Domain or loading objects.
std::string UnsupportedSchema(std::filesystem::path const& state_dir) {
  for (auto const& entry : std::filesystem::recursive_directory_iterator(state_dir)) {
    if (entry.is_directory()) {
      continue;
    }
    if (!entry.is_regular_file()) {
      throw std::runtime_error("non-file entry in fixture storage");
    }
    std::vector<std::string> parts;
    for (auto const& part : entry.path().lexically_relative(state_dir)) {
      parts.push_back(part.string());
    }
    if (parts.size() != 3) {
      throw std::runtime_error("unexpected file in fixture storage: " +
                               entry.path().string());
    }
    auto const object_id = ParseNumber(parts[0]);
    auto const class_id = ParseNumber(parts[1]);
    auto const schema = ParseNumber(parts[2]);
    unsigned supported = 0;
    std::string_view class_name;
    if (class_id == LayerBase::kClassId) {
      class_name = "LayerBase";
      supported = kBaseSchema;
    } else if (class_id == LayerDerived::kClassId) {
      class_name = "LayerDerived";
      supported = kDerivedSchema;
    } else if (class_id == StateRoot::kClassId) {
      class_name = "StateRoot";
    } else if (class_id == ae::Obj::kClassId) {
      class_name = "ae::Obj";
    } else {
      return "object=" + std::to_string(object_id) + " unknown class=" +
             std::to_string(class_id);
    }
    if (schema > supported) {
      return "object=" + std::to_string(object_id) + " class=" +
             std::string(class_name) + "(" + std::to_string(class_id) +
             ") stored_schema=" + std::to_string(schema) +
             " supported_max=" + std::to_string(supported);
    }
  }
  return {};
}

int Run(int argc, char** argv) {
  std::filesystem::path state_dir;
  std::string operation;
  bool native = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view const arg{argv[i]};
    if (arg == "--native") {
      native = true;
    } else if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--op" && i + 1 < argc) {
      operation = argv[++i];
    } else {
      throw std::runtime_error("unknown or incomplete argument: " + std::string(arg));
    }
  }
  if (state_dir.empty() || (operation != "create" && operation != "inspect" &&
                            operation != "update")) {
    throw std::runtime_error(
        "usage: --state-dir PATH --op create|inspect|update [--native]");
  }

  if (operation == "create") {
    if (std::filesystem::exists(state_dir) &&
        (!std::filesystem::is_directory(state_dir) ||
         !std::filesystem::is_empty(state_dir))) {
      throw std::runtime_error("create requires a new or empty state directory");
    }
  } else {
    if (!std::filesystem::is_directory(state_dir)) {
      throw std::runtime_error("state directory does not exist");
    }
    if (!native) {
      auto const reason = UnsupportedSchema(state_dir);
      if (!reason.empty()) {
        std::cerr << "INCOMPATIBLE exe=" << kExe << ' ' << reason << '\n';
        return 2;
      }
    }
  }

  EnsureObjectRegistration();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  ae::Ptr<StateRoot> root;
  if (operation == "create") {
    auto root_ref = StateRoot::ptr::Create(ae::CreateWith{domain}.with_id(kRootId));
    auto child = LayerDerived::ptr::Create(ae::CreateWith{domain}.with_id(kChildId));
    child->value = 11;
    child->child_value = 37;
    child->label = "initial";
    root_ref->child = child;
    root_ref->alias_child = child;
    child->backref = root_ref;
    root = root_ref.Load();
  } else {
    ae::DomainGraph graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(kRootId, loaded).IsOk() || !loaded ||
        loaded->GetClassId() != StateRoot::kClassId) {
      throw std::runtime_error("native LoadRoot failed or root class mismatched");
    }
    root = ae::Ptr<StateRoot>{loaded};
  }

  auto child = root->child.Load();
  auto alias_child = root->alias_child.Load();
  if (!child || !alias_child || root->obj_id.id() != kRootId ||
      child->obj_id.id() != kChildId ||
      child->GetClassId() != LayerDerived::kClassId ||
      child.get() != alias_child.get() || child->child_value != 37) {
    throw std::runtime_error("object identity, alias or child value changed");
  }
  auto backref = child->backref.Load();
  if (!backref || backref.get() != root.get()) {
    throw std::runtime_error("child-to-root cycle changed");
  }

  if (operation == "update") {
    child->value += kStep[APPTRAVERSE_EXE_REVISION];
    child->label = std::string("updated-") + kExe;
  }
  if (operation != "inspect") {
#if APPTRAVERSE_SCHEMA_REVISION >= 1
    child->added_base = 1000 + child->value;
#endif
#if APPTRAVERSE_SCHEMA_REVISION >= 2
    child->added_derived = 2000 + child->value;
#endif
    storage.BeginSaveSession();
    ae::DomainGraph graph{&domain};
    if (!graph.SaveRoot(root, kRootId).IsOk() || storage.SaveSessionHadFailure()) {
      throw std::runtime_error("SaveRoot failed");
    }
  }

  std::uint32_t added_base = 0;
  std::uint32_t added_derived = 0;
#if APPTRAVERSE_SCHEMA_REVISION >= 1
  added_base = child->added_base;
#endif
#if APPTRAVERSE_SCHEMA_REVISION >= 2
  added_derived = child->added_derived;
#endif
  std::cout << "{\"exe\":\"" << kExe << "\",\"mode\":\""
            << (APPTRAVERSE_EVOLUTION_MODE ? "evolving" : "stable")
            << "\",\"base_schema\":" << kBaseSchema
            << ",\"derived_schema\":" << kDerivedSchema
            << ",\"value\":" << child->value
            << ",\"label\":" << std::quoted(child->label)
            << ",\"child_value\":" << child->child_value
            << ",\"added_base\":" << added_base
            << ",\"added_derived\":" << added_derived
            << ",\"root_id\":" << root->obj_id.id()
            << ",\"child_id\":" << root->child.id().id()
            << ",\"alias_child_id\":" << root->alias_child.id().id()
            << ",\"child_backref_id\":" << child->backref.id().id()
            << ",\"root_class_id\":" << StateRoot::kClassId
            << ",\"base_class_id\":" << LayerBase::kClassId
            << ",\"derived_class_id\":" << LayerDerived::kClassId
            << ",\"aliases_same_object\":true,\"backref_is_root\":true}\n";
  return 0;
}

}  // namespace apptraverse::test::application_evolution

int main(int argc, char** argv) {
  try {
    return apptraverse::test::application_evolution::Run(argc, argv);
  } catch (std::exception const& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
