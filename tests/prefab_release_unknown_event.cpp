#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#ifndef APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION
#error APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION required
#endif

namespace apptraverse::test::prefab_release_unknown {
namespace {

constexpr ae::ObjId::Type kFlatEventRootId = 62001;
constexpr ae::ObjId::Type kJournalTargetId = 62002;

class FutureUnknownEvent;
class FutureUnknownJournalEvent;

class UnknownProbeTarget : public NodeFor<UnknownProbeTarget> {
  APPTRAVERSE_OBJECT(UnknownProbeTarget, Node, 0)

 protected:
  UnknownProbeTarget() = default;

 public:
  explicit UnknownProbeTarget(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tag);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tag);
  }

  void Apply(FutureUnknownEvent const& event);

  std::uint32_t tag{0};
};

class UnknownJournalTarget : public NodeFor<UnknownJournalTarget> {
  APPTRAVERSE_OBJECT(UnknownJournalTarget, Node, 0)

 protected:
  UnknownJournalTarget() = default;

 public:
  explicit UnknownJournalTarget(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tag);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tag);
  }

  void Apply(FutureUnknownJournalEvent const& event);

  std::uint32_t tag{0};
};

class FutureUnknownEvent : public EventFor<UnknownProbeTarget, FutureUnknownEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::prefab_release_unknown::FutureUnknownEvent",
      FutureUnknownEvent, Event, 0)

 protected:
  FutureUnknownEvent() = default;

 public:
  explicit FutureUnknownEvent(ae::ObjProp prop) : EventFor{prop} {}

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

void UnknownProbeTarget::Apply(FutureUnknownEvent const& event) {
  tag = event.payload;
}

class FutureUnknownJournalEvent
    : public EventFor<UnknownJournalTarget, FutureUnknownJournalEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::prefab_release_unknown::FutureUnknownJournalEvent",
      FutureUnknownJournalEvent, Event, 0)

 protected:
  FutureUnknownJournalEvent() = default;

 public:
  explicit FutureUnknownJournalEvent(ae::ObjProp prop) : EventFor{prop} {}

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

void UnknownJournalTarget::Apply(FutureUnknownJournalEvent const& event) {
  tag = event.payload;
}

APPTRAVERSE_REGISTER(UnknownProbeTarget);
APPTRAVERSE_REGISTER(UnknownJournalTarget);
#if APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION >= 1
APPTRAVERSE_REGISTER(FutureUnknownEvent);
APPTRAVERSE_REGISTER(FutureUnknownJournalEvent);
#endif

void PrintJson(std::string const& body) { std::cout << body << '\n'; }

int SaveEventRoot(ae::DomainGraph& graph, DirectoryDomainStorage& storage,
                  ae::Ptr<ae::Obj> const& event, ae::ObjId root_id) {
  storage.BeginSaveSession();
  if (!graph.SaveRoot(event, root_id).IsOk() || storage.SaveSessionHadFailure()) {
    return 1;
  }
  return 0;
}

}  // namespace
}  // namespace apptraverse::test::prefab_release_unknown

int main(int argc, char** argv) {
  using namespace apptraverse::test::prefab_release_unknown;
  apptraverse::EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_UnknownProbeTarget;
  (void)&g_apptraverse_registrar_UnknownJournalTarget;
#if APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION >= 1
  (void)&g_apptraverse_registrar_FutureUnknownEvent;
  (void)&g_apptraverse_registrar_FutureUnknownJournalEvent;
#endif

  std::filesystem::path state_dir;
  std::string op;
  for (int i = 1; i < argc; ++i) {
    std::string arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--op" && i + 1 < argc) {
      op = argv[++i];
    }
  }
  if (state_dir.empty() || op.empty()) {
    return 2;
  }

  apptraverse::DirectoryDomainStorage state_storage{state_dir};
  ae::Domain domain{state_storage};
  ae::DomainGraph graph{&domain};

  if (op == "write-unknown") {
#if APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION >= 1
    auto created = FutureUnknownEvent::ptr::Create(
        ae::CreateWith{domain}.with_id(ae::ObjId{kFlatEventRootId}));
    ae::Ptr<FutureUnknownEvent> event = created.Load();
    event->payload = 42;
    if (SaveEventRoot(graph, state_storage, event,
                      ae::ObjId{kFlatEventRootId}) != 0) {
      PrintJson("{\"ok\":false}");
      return 1;
    }
    PrintJson("{\"ok\":true,\"object_id\":" + std::to_string(kFlatEventRootId) +
              ",\"probe_class_id\":" +
              std::to_string(FutureUnknownEvent::kClassId) + "}");
    return 0;
#else
    PrintJson("{\"ok\":false,\"error\":\"unsupported\"}");
    return 1;
#endif
  }

  if (op == "write-journal-unknown") {
#if APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION >= 1
    auto target_ref = UnknownJournalTarget::ptr::Create(
        ae::CreateWith{domain}.with_id(ae::ObjId{kJournalTargetId}));
    ae::Ptr<UnknownJournalTarget> target = target_ref.Load();
    FinalizeDistilledGraph(*target);
    auto event =
        FutureUnknownJournalEvent::ptr::Create(ae::CreateWith{domain});
    event.Load()->payload = 42;
    target->Commit(event);
    state_storage.BeginSaveSession();
    ae::Ptr<ae::Obj> root{target};
    if (!graph.SaveRoot(root, ae::ObjId{kJournalTargetId}).IsOk() ||
        state_storage.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    PrintJson("{\"ok\":true,\"object_id\":" + std::to_string(kJournalTargetId) +
              ",\"journal_len\":" + std::to_string(target->journal.size()) +
              ",\"tag\":" + std::to_string(target->tag) +               ",\"event_class_id\":" +
              std::to_string(FutureUnknownJournalEvent::kClassId) + "}");
    return 0;
#else
    PrintJson("{\"ok\":false,\"error\":\"unsupported\"}");
    return 1;
#endif
  }

  if (op == "load-verify") {
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kFlatEventRootId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-failed\"}");
      return 1;
    }
#if APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION >= 1
    if (loaded->GetClassId() != FutureUnknownEvent::kClassId) {
      PrintJson("{\"ok\":false,\"error\":\"class-mismatch\"}");
      return 1;
    }
    ae::Ptr<FutureUnknownEvent> event{loaded};
    PrintJson("{\"ok\":true,\"payload\":" + std::to_string(event->payload) + "}");
    return 0;
#else
    PrintJson("{\"ok\":false,\"error\":\"unsupported\"}");
    return 1;
#endif
  }

  if (op == "load-journal-target") {
    ae::Ptr<ae::Obj> loaded;
    auto const result = graph.LoadRoot(ae::ObjId{kJournalTargetId}, loaded);
    if (!result.IsOk() || !loaded) {
      PrintJson("{\"ok\":false,\"stage\":\"load\",\"error\":\"LoadRoot\"}");
      return 2;
    }
    PrintJson("{\"ok\":true,\"stage\":\"load\"}");
    return 0;
  }

  if (op == "verify-journal-target") {
#if APPTRAVERSE_PREFAB_UNKNOWN_EXE_REVISION >= 1
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kJournalTargetId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-failed\"}");
      return 1;
    }
    ae::Ptr<UnknownJournalTarget> target{loaded};
    std::uint32_t const replay_tag = target->tag;
    target->ReplayFromBase();
    std::uint32_t const replay_once = target->tag;
    target->ReplayFromBase();
    std::uint32_t const replay_twice = target->tag;
    PrintJson("{\"ok\":true,\"journal_len\":" +
              std::to_string(target->journal.size()) + ",\"tag\":" +
              std::to_string(replay_tag) + ",\"event_class_id\":" +
              std::to_string(FutureUnknownJournalEvent::kClassId) +
              ",\"payload\":" + std::to_string(replay_tag) +
              ",\"replay_tag\":" + std::to_string(replay_once) +
              ",\"replay_twice\":" + std::to_string(replay_twice) + "}");
    return 0;
#else
    PrintJson("{\"ok\":false,\"error\":\"unsupported\"}");
    return 1;
#endif
  }

  if (op == "resave") {
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kFlatEventRootId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"stage\":\"load\",\"error\":\"LoadRoot\"}");
      return 2;
    }
    state_storage.BeginSaveSession();
    if (!graph.SaveRoot(loaded, ae::ObjId{kFlatEventRootId}).IsOk() ||
        state_storage.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"stage\":\"save\",\"error\":\"SaveRoot\"}");
      return 2;
    }
    PrintJson("{\"ok\":true}");
    return 0;
  }

  return 2;
}
