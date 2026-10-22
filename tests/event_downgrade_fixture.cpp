#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"

#ifndef APPTRAVERSE_EVENT_EXE_REVISION
#error APPTRAVERSE_EVENT_EXE_REVISION is required
#endif
static_assert(APPTRAVERSE_EVENT_EXE_REVISION == 0 ||
              APPTRAVERSE_EVENT_EXE_REVISION == 1);

namespace apptraverse::test::event_downgrade {

constexpr ae::ObjId::Type kEventObjId = 93001;

class DowngradeProbeEvent;

class DowngradeProbeTarget : public Node {
  APPTRAVERSE_OBJECT(DowngradeProbeTarget, Node, 0)

 protected:
  DowngradeProbeTarget() = default;

 public:
  explicit DowngradeProbeTarget(ae::ObjProp prop) : Node{prop} {}

  AE_OBJECT_REFLECT()

  void Apply(DowngradeProbeEvent const& event);
};

class DowngradeProbeEvent
    : public EventFor<DowngradeProbeTarget, DowngradeProbeEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::event_downgrade::DowngradeProbeEvent",
      DowngradeProbeEvent, Event, 0)

 protected:
  DowngradeProbeEvent() = default;

 public:
  explicit DowngradeProbeEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(payload))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, payload);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, payload);
  }

  std::uint32_t payload{0};
};

inline void DowngradeProbeTarget::Apply(DowngradeProbeEvent const& event) {
  (void)event;
}

APPTRAVERSE_REGISTER(DowngradeProbeTarget);

#if APPTRAVERSE_EVENT_EXE_REVISION == 1
APPTRAVERSE_REGISTER(DowngradeProbeEvent);
#endif

void EnsureRegistration() { EnsureObjectRegistration(); }

int Run(int argc, char** argv) {
  std::filesystem::path state_dir;
  std::string operation;
  for (int i = 1; i < argc; ++i) {
    std::string_view const argument{argv[i]};
    if (argument == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (argument == "--op" && i + 1 < argc) {
      operation = argv[++i];
    } else {
      throw std::runtime_error("invalid or incomplete argument");
    }
  }
  if (state_dir.empty() ||
      (operation != "write-unknown" && operation != "resave" &&
       operation != "inspect")) {
    throw std::runtime_error(
        "usage: --state-dir PATH --op write-unknown|resave|inspect");
  }

  EnsureRegistration();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};

#if APPTRAVERSE_EVENT_EXE_REVISION == 1
  if (operation == "write-unknown") {
    if (std::filesystem::exists(state_dir) &&
        (!std::filesystem::is_directory(state_dir) ||
         !std::filesystem::is_empty(state_dir))) {
      throw std::runtime_error("write-unknown requires an empty directory");
    }
    auto created = DowngradeProbeEvent::ptr::Create(
        ae::CreateWith{domain}.with_id(ae::ObjId{kEventObjId}));
    auto event = created.Load();
    event->payload = 0xA1B2C3D4u;
    storage.BeginSaveSession();
    if (!graph.SaveRoot(event, ae::ObjId{kEventObjId}).IsOk() ||
        storage.SaveSessionHadFailure()) {
      throw std::runtime_error("save probe event failed");
    }
    std::cout << "{\"ok\":true,\"probe_class_id\":"
              << DowngradeProbeEvent::kClassId << ",\"object_id\":"
              << kEventObjId << "}\n";
    return 0;
  }
#else
  if (operation == "write-unknown") {
    throw std::runtime_error("EXE A cannot write unknown event");
  }
#endif

  if (operation == "resave") {
    ae::Ptr<ae::Obj> loaded;
    auto const load = graph.LoadRoot(ae::ObjId{kEventObjId}, loaded);
    if (!load.IsOk() || !loaded) {
      std::cout << "{\"ok\":false,\"stage\":\"load\",\"error\":\"LoadRoot\"}\n";
      return 2;
    }
    std::cout << "{\"ok\":true,\"stage\":\"load\",\"loaded_class_id\":"
              << loaded->GetClassId() << ",\"object_id\":" << kEventObjId
              << "}\n";
    storage.BeginSaveSession();
    if (!graph.SaveRoot(loaded, ae::ObjId{kEventObjId}).IsOk() ||
        storage.SaveSessionHadFailure()) {
      std::cout << "{\"ok\":false,\"stage\":\"save\",\"error\":\"SaveRoot\"}\n";
      return 2;
    }
    return 0;
  }

  // inspect: list class layers for the event object (no LoadRoot).
  auto const layers = storage.Enumerate(ae::ObjId{kEventObjId});
  std::cout << "{\"ok\":true,\"object_id\":" << kEventObjId
            << ",\"class_ids\":[";
  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (i != 0) {
      std::cout << ',';
    }
    std::cout << layers[i];
  }
  std::cout << "]}";
#if APPTRAVERSE_EVENT_EXE_REVISION == 1
  std::cout << ",\"probe_class_id\":" << DowngradeProbeEvent::kClassId;
#endif
  std::cout << "\n";
  return 0;
}

}  // namespace apptraverse::test::event_downgrade

int main(int argc, char** argv) {
  try {
    return apptraverse::test::event_downgrade::Run(argc, argv);
  } catch (std::exception const& ex) {
    std::cerr << ex.what() << "\n";
    return 1;
  }
}
