#include "prefab_release_model.h"

#include <cstdlib>

#include "aether-objects/obj/obj.h"

#include "apptraverse/distill.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/node.h"
#include "apptraverse/presenter.h"

#ifndef APPTRAVERSE_PREFAB_EXE_REVISION
#define APPTRAVERSE_PREFAB_EXE_REVISION 0
#endif

namespace apptraverse::test::prefab_release {

std::uint32_t g_lazy_load_count{0};

class ConstLabelString;
class PrefabLocalization;
class PrefabWindow;
class LayoutNode;
class CounterNode;
class DetailsNode;
class HistoryNode;
class PreferencesNode;
class WorkspaceRoot;
class NullPresenterNode;

class SetPosxAbsoluteEvent;
class SetPosxDeltaEvent;

class ConstLabelString : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::prefab_release::ConstLabelString",
                           ConstLabelString, ae::Obj, 0)

 protected:
  ConstLabelString() = default;

 public:
  explicit ConstLabelString(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text);
  }

  std::string text;
};

class PrefabLocalization : public NodeFor<PrefabLocalization> {
  APPTRAVERSE_OBJECT(PrefabLocalization, Node, 0)

 protected:
  PrefabLocalization() = default;

 public:
  explicit PrefabLocalization(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(strings))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, strings);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, strings);
  }

  std::vector<ae::ObjPtr<ConstLabelString>> strings;
};

class PrefabWindow : public NodeFor<PrefabWindow> {
  APPTRAVERSE_OBJECT(PrefabWindow, Node,
                     APPTRAVERSE_PREFAB_EXE_REVISION)

 protected:
  PrefabWindow() = default;

 public:
  explicit PrefabWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(prefab_origin), AE_MMBR(width), AE_MMBR(posx),
                    AE_MMBR(user_owned_s0), AE_MMBR(user_owned_s1))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    ++g_lazy_load_count;
    dnv(base_, prefab_origin, width, user_owned_s0);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, prefab_origin, width, user_owned_s0);
  }

#if APPTRAVERSE_PREFAB_EXE_REVISION >= 1
  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, posx, user_owned_s1);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, posx, user_owned_s1);
  }
#endif

  void Apply(SetPosxAbsoluteEvent const& event);
  void Apply(SetPosxDeltaEvent const& event);

  ae::ObjId prefab_origin{};
  std::uint32_t width{0};
  std::int32_t posx{0};
  bool user_owned_s0{false};
  bool user_owned_s1{false};
};

class NullPresenterNode : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::prefab_release::NullPresenterNode",
                           NullPresenterNode, Presenter, 0)

 protected:
  NullPresenterNode() = default;

 public:
  explicit NullPresenterNode(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(localization), AE_MMBR(window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, localization, window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, localization, window);
  }

  ae::ObjPtr<PrefabLocalization> localization;
  ae::ObjPtr<PrefabWindow> window;
};

class LayoutNode : public NodeFor<LayoutNode> {
  APPTRAVERSE_OBJECT(LayoutNode, Node, 0)

 protected:
  LayoutNode() = default;

 public:
  explicit LayoutNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(counter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, counter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, counter);
  }

  ae::ObjPtr<CounterNode> counter;
};

class CounterNode : public NodeFor<CounterNode> {
  APPTRAVERSE_OBJECT(CounterNode, Node, 0)

 protected:
  CounterNode() = default;

 public:
  explicit CounterNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  std::int32_t value{0};
};

class DetailsNode : public NodeFor<DetailsNode> {
  APPTRAVERSE_OBJECT(DetailsNode, Node, 0)

 protected:
  DetailsNode() = default;

 public:
  explicit DetailsNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(history))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, history);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, history);
  }

  ae::ObjPtr<HistoryNode> history;
};

class HistoryNode : public NodeFor<HistoryNode> {
  APPTRAVERSE_OBJECT(HistoryNode, Node, 0)

 protected:
  HistoryNode() = default;

 public:
  explicit HistoryNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(note))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, note);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, note);
  }

  std::string note;
};

class PreferencesNode : public NodeFor<PreferencesNode> {
  APPTRAVERSE_OBJECT(PreferencesNode, Node, 0)

 protected:
  PreferencesNode() = default;

 public:
  explicit PreferencesNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_a_alias))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_a_alias);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_a_alias);
  }

  ae::ObjPtr<PrefabWindow> window_a_alias;
};

class WorkspaceRoot : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::prefab_release::WorkspaceRoot",
                           WorkspaceRoot, ae::Obj, 0)

 protected:
  WorkspaceRoot() = default;

 public:
  explicit WorkspaceRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_a), AE_MMBR(window_b), AE_MMBR(window_c),
                    AE_MMBR(presenter), AE_MMBR(layout), AE_MMBR(details),
                    AE_MMBR(preferences))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_a, window_b, window_c, presenter, layout, details,
        preferences);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_a, window_b, window_c, presenter, layout, details,
        preferences);
  }

  ae::ObjPtr<PrefabWindow> window_a;
  ae::ObjPtr<PrefabWindow> window_b;
  ae::ObjPtr<PrefabWindow> window_c;
  ae::ObjPtr<NullPresenterNode> presenter;
  ae::ObjPtr<LayoutNode> layout;
  ae::ObjPtr<DetailsNode> details;
  ae::ObjPtr<PreferencesNode> preferences;
};

class SetPosxAbsoluteEvent
    : public EventFor<PrefabWindow, SetPosxAbsoluteEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::prefab_release::SetPosxAbsoluteEvent",
      SetPosxAbsoluteEvent, Event, 0)

 protected:
  SetPosxAbsoluteEvent() = default;

 public:
  explicit SetPosxAbsoluteEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(posx))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, posx);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, posx);
  }

  std::int32_t posx{0};
};

class SetPosxDeltaEvent : public EventFor<PrefabWindow, SetPosxDeltaEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::prefab_release::SetPosxDeltaEvent",
      SetPosxDeltaEvent, Event, 0)

 protected:
  SetPosxDeltaEvent() = default;

 public:
  explicit SetPosxDeltaEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, delta);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, delta);
  }

  std::int32_t delta{0};
};

void PrefabWindow::Apply(SetPosxAbsoluteEvent const& event) {
  posx = event.posx;
  user_owned_s1 = true;
}

void PrefabWindow::Apply(SetPosxDeltaEvent const& event) {
  posx += event.delta;
  user_owned_s1 = true;
}

APPTRAVERSE_REGISTER(ConstLabelString);
APPTRAVERSE_REGISTER(PrefabLocalization);
APPTRAVERSE_REGISTER(PrefabWindow);
APPTRAVERSE_REGISTER(NullPresenterNode);
APPTRAVERSE_REGISTER(LayoutNode);
APPTRAVERSE_REGISTER(CounterNode);
APPTRAVERSE_REGISTER(DetailsNode);
APPTRAVERSE_REGISTER(HistoryNode);
APPTRAVERSE_REGISTER(PreferencesNode);
APPTRAVERSE_REGISTER(WorkspaceRoot);
APPTRAVERSE_REGISTER(SetPosxAbsoluteEvent);
APPTRAVERSE_REGISTER(SetPosxDeltaEvent);

namespace {

struct BuiltPrefab {
  ae::ObjPtr<PrefabWindow> window;
  ae::ObjPtr<PrefabLocalization> localization;
  std::vector<ae::ObjId> const_string_ids;
};

BuiltPrefab BuildSinglePrefab(ae::Domain& domain, ae::ObjId prefab_id,
                              PrefabDefaults const& defaults,
                              ae::ObjId label_const_id, char const* label) {
  BuiltPrefab out;
  out.window = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(prefab_id));
  ae::Ptr<PrefabWindow> window = out.window.Load();
  window->prefab_origin = ae::ObjId{};
  window->width = defaults.width;
  window->posx = defaults.posx;

  auto label_obj = ConstLabelString::ptr::Create(
      ae::CreateWith{domain}.with_id(label_const_id));
  label_obj.Load()->text = label;
  out.const_string_ids.push_back(label_const_id);

  out.localization = PrefabLocalization::ptr::Create(ae::CreateWith{domain});
  out.localization.Load()->strings.push_back(label_obj);

  FinalizeDistilledGraph(*window);
  StabilizeDistilledPersistentSnapshot(*window);
  SaveDistilledRoot(*window);

  return out;
}

}  // namespace

void EnsurePrefabReleaseRegistration() {
  apptraverse::EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_ConstLabelString;
  (void)&g_apptraverse_registrar_PrefabLocalization;
  (void)&g_apptraverse_registrar_PrefabWindow;
  (void)&g_apptraverse_registrar_NullPresenterNode;
  (void)&g_apptraverse_registrar_LayoutNode;
  (void)&g_apptraverse_registrar_CounterNode;
  (void)&g_apptraverse_registrar_DetailsNode;
  (void)&g_apptraverse_registrar_HistoryNode;
  (void)&g_apptraverse_registrar_PreferencesNode;
  (void)&g_apptraverse_registrar_WorkspaceRoot;
  (void)&g_apptraverse_registrar_SetPosxAbsoluteEvent;
  (void)&g_apptraverse_registrar_SetPosxDeltaEvent;
}

PrefabDefaults MainWindowPrefabDefaults() {
  return {.width = 640, .posx = 10};
}

PrefabDefaults ToolWindowPrefabDefaults() {
  return {.width = 480, .posx = 20};
}

ae::RamDomainStorage BuildPrefabSupplyGraph() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  EnsurePrefabReleaseRegistration();
  PrefabPtr<PrefabWindow>::ClearOrigins();

  (void)BuildSinglePrefab(domain, ae::ObjId{kMainWindowPrefabId},
                          MainWindowPrefabDefaults(), ae::ObjId{kMainLabelConstId},
                          "main-title");
  (void)BuildSinglePrefab(domain, ae::ObjId{kToolWindowPrefabId},
                          ToolWindowPrefabDefaults(), ae::ObjId{kToolLabelConstId},
                          "tool-title");

  return storage;
}

ClonePlan PlanCloneFromPrefabGraph(ae::Domain const& domain) {
  ClonePlan plan;
  plan.supply_constant_ids = {ae::ObjId{kMainLabelConstId},
                              ae::ObjId{kToolLabelConstId}};
  plan.instance_remap[kMainWindowPrefabId] = kWindowAId;
  plan.instance_remap[kMainWindowPrefabId + 100000] = kWindowBId;
  (void)domain;
  return plan;
}

ae::ObjPtr<ae::Obj> CloneWorkspaceFromPrefabs(ae::Domain& domain,
                                               ClonePlan const& plan,
                                               bool create_window_c) {
  PrefabPtr<PrefabWindow>::ClearOrigins();
  EnsurePrefabReleaseRegistration();

  WorkspaceRoot::ptr workspace = WorkspaceRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId{kWorkspaceId}));

  auto window_a_ref = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId{kWindowAId}));
  ae::Ptr<PrefabWindow> window_a = window_a_ref.Load();
  window_a->prefab_origin = ae::ObjId{kMainWindowPrefabId};
  window_a->width = MainWindowPrefabDefaults().width;
  window_a->posx = MainWindowPrefabDefaults().posx;
  PrefabPtr<PrefabWindow>::RegisterOrigin(ae::ObjId{kWindowAId},
                                          ae::ObjId{kMainWindowPrefabId});

  auto window_b_ref = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}
          .with_id(ae::ObjId{kWindowBId})
          .with_flags(ae::ObjFlags::kUnloadedByDefault));
  ae::Ptr<PrefabWindow> window_b = window_b_ref.Load();
  window_b->prefab_origin = ae::ObjId{kMainWindowPrefabId};
  window_b->width = MainWindowPrefabDefaults().width;
  window_b->posx = MainWindowPrefabDefaults().posx;
  PrefabPtr<PrefabWindow>::RegisterOrigin(ae::ObjId{kWindowBId},
                                          ae::ObjId{kMainWindowPrefabId});

  ae::ObjPtr<PrefabWindow> window_c_ref;
  if (create_window_c) {
    window_c_ref = PrefabWindow::ptr::Create(
        ae::CreateWith{domain}.with_id(ae::ObjId{kWindowCId}));
    ae::Ptr<PrefabWindow> window_c = window_c_ref.Load();
    window_c->prefab_origin = ae::ObjId{kToolWindowPrefabId};
    window_c->width = ToolWindowPrefabDefaults().width;
    window_c->posx = ToolWindowPrefabDefaults().posx;
    PrefabPtr<PrefabWindow>::RegisterOrigin(ae::ObjId{kWindowCId},
                                            ae::ObjId{kToolWindowPrefabId});
  }

  auto presenter = NullPresenterNode::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId{kNullPresenterId}));
  presenter.Load()->window = window_a_ref;

  auto layout = LayoutNode::ptr::Create(ae::CreateWith{domain}.with_id(kLayoutId));
  layout.Load()->counter =
      CounterNode::ptr::Create(ae::CreateWith{domain}.with_id(kCounterId));

  auto details = DetailsNode::ptr::Create(
      ae::CreateWith{domain}.with_id(kDetailsId));
  details.Load()->history = HistoryNode::ptr::Create(
      ae::CreateWith{domain}
          .with_id(kHistoryId)
          .with_flags(ae::ObjFlags::kUnloadedByDefault));

  auto preferences = PreferencesNode::ptr::Create(
      ae::CreateWith{domain}.with_id(kPreferencesId));
  preferences.Load()->window_a_alias = window_a_ref;

  ae::Ptr<WorkspaceRoot> root = workspace.Load();
  root->window_a = window_a_ref;
  root->window_b = window_b_ref;
  root->window_c = window_c_ref;
  root->presenter = presenter;
  root->layout = layout;
  root->details = details;
  root->preferences = preferences;

  (void)plan;
  return ae::ObjPtr<ae::Obj>{workspace};
}

std::uint32_t LazyLoadCount() { return g_lazy_load_count; }

void ResetLazyLoadCount() { g_lazy_load_count = 0; }

}  // namespace apptraverse::test::prefab_release

#if defined(APPTRAVERSE_PREFAB_FIXTURE_IMPL)

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"

#include "embedded_distilled_prefab_release_supply.h"

namespace apptraverse::test::prefab_release {
namespace {

void PrintJson(std::string const& body) { std::cout << body << '\n'; }

std::string JsonOk(std::string_view fields) {
  return std::string{"{\"ok\":true,"} + std::string{fields} + "}";
}

struct FixtureStorage {
  ae::RamDomainStorage backing;
  DirectoryDomainStorage user;
  OverlayDomainStorage overlay;
  ae::Domain domain;

  explicit FixtureStorage(std::filesystem::path const& user_root)
      : user{user_root},
        overlay{user, backing,
                EmbeddedSupplyConstantIdSet(
                    embedded_distilled::prefab_release::kEmbeddedGraph)},
        domain{overlay} {
    InstallEmbeddedDistilledBacking(
        backing, embedded_distilled::prefab_release::kEmbeddedGraph);
  }
};

bool UserHasLayer(DirectoryDomainStorage const& user, ae::ObjId object_id,
                  std::uint32_t class_id, std::uint8_t version) {
  return DirectoryDomainStorageLayerFileExists(
      user.root(), ae::DomainQuery{object_id, class_id, version});
}

std::optional<std::int32_t> ReadPrefabPosxFromBacking(
    ae::RamDomainStorage const& backing, ae::ObjId prefab_id) {
#if APPTRAVERSE_PREFAB_EXE_REVISION >= 1
  ae::Domain backing_domain{const_cast<ae::RamDomainStorage&>(backing)};
  PrefabWindow::ptr ref =
      PrefabWindow::ptr::Declare(ae::CreateWith{backing_domain}.with_id(prefab_id));
  if (!ref.Load()) {
    return std::nullopt;
  }
  return ref.Load()->posx;
#else
  (void)backing;
  (void)prefab_id;
  return std::nullopt;
#endif
}

void ApplyPrefabS1Fallback(PrefabWindow& window,
                           ae::RamDomainStorage const& backing) {
#if APPTRAVERSE_PREFAB_EXE_REVISION >= 1
  if (window.user_owned_s1) {
    return;
  }
  if (!window.prefab_origin.is_valid()) {
    return;
  }
  if (auto posx = ReadPrefabPosxFromBacking(backing, window.prefab_origin)) {
    window.posx = *posx;
  }
#else
  (void)window;
  (void)backing;
#endif
}

}  // namespace

int RunPrefabReleaseFixture(int argc, char** argv) {
  EnsurePrefabReleaseRegistration();

  std::filesystem::path state_dir;
  std::string op;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--op" && i + 1 < argc) {
      op = argv[++i];
    } else {
      PrintJson("{\"ok\":false,\"error\":\"bad-args\"}");
      return 2;
    }
  }
  if (state_dir.empty() || op.empty()) {
    PrintJson("{\"ok\":false,\"error\":\"usage\"}");
    return 2;
  }

  if (op == "r0-init") {
    if (std::filesystem::exists(state_dir) &&
        (!std::filesystem::is_directory(state_dir) ||
         !std::filesystem::is_empty(state_dir))) {
      PrintJson("{\"ok\":false,\"error\":\"state-not-empty\"}");
      return 1;
    }
    DirectoryDomainStorage user{state_dir};
    ae::Domain domain{user};
    ae::ObjPtr<ae::Obj> ws_root = CloneWorkspaceFromPrefabs(domain, {}, false);
    ae::Ptr<ae::Obj> ws_obj = ws_root.Load();
    ae::Ptr<WorkspaceRoot> ws{ws_obj};
    if (!ws) {
      PrintJson("{\"ok\":false,\"error\":\"missing-workspace\"}");
      return 1;
    }
    FinalizeDistilledGraph(*ws);
    SaveDistilledRoot(*ws);
    if (user.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    PrintJson(JsonOk("\"op\":\"r0-init\",\"lazy_load_count\":" +
                     std::to_string(LazyLoadCount())));
    return 0;
  }

  if (op == "r2-verify") {
    ResetLazyLoadCount();
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    ae::Ptr<PrefabWindow> a = ws->window_a.Load();
    ApplyPrefabS1Fallback(*a, stores.backing);
    bool const b_loaded_before = ws->window_b.is_loaded();
    PrintJson(JsonOk(
        "\"op\":\"r2-verify\""
        ",\"width_a\":" +
        std::to_string(a->width) +
        ",\"posx_a\":" + std::to_string(a->posx) +
        ",\"window_b_loaded\":" + (b_loaded_before ? "true" : "false") +
        ",\"lazy_load_count\":" + std::to_string(LazyLoadCount()) +
        ",\"prefab_origin_a\":" + std::to_string(a->prefab_origin.id()) +
        ",\"alias_ok\":" +
        (ws->preferences.Load()->window_a_alias.id() == ws->window_a.id()
             ? "true"
             : "false")));
    return 0;
  }

  if (op == "r2-set-posx-a" || op == "r2-set-posx-delta-a") {
    bool const delta = op == "r2-set-posx-delta-a";
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    ae::Ptr<PrefabWindow> a = ws->window_a.Load();
    if (delta) {
      auto event = SetPosxDeltaEvent::ptr::Create(ae::CreateWith{stores.domain});
      event.Load()->delta = 5;
      a->Commit(event);
    } else {
      auto event =
          SetPosxAbsoluteEvent::ptr::Create(ae::CreateWith{stores.domain});
      event.Load()->posx = 99;
      a->Commit(event);
    }
    stores.user.BeginSaveSession();
    ae::DomainGraph save_graph{&stores.domain};
    if (!save_graph.SaveRoot(loaded, ae::ObjId{kWorkspaceId}).IsOk() ||
        stores.user.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    bool const user_s1 = UserHasLayer(
        stores.user, ae::ObjId{kWindowAId}, PrefabWindow::kClassId, 1);
    PrintJson(JsonOk(std::string("\"op\":\"r2-set-posx\",\"user_s1_a\":") +
                     (user_s1 ? "true" : "false") + ",\"posx_a\":" +
                     std::to_string(a->posx)));
    return 0;
  }

  if (op == "r2-load-lazy-b") {
    ResetLazyLoadCount();
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded);
    ae::Ptr<WorkspaceRoot> ws{loaded};
    ae::Ptr<PrefabWindow> b = ws->window_b.Load();
    ApplyPrefabS1Fallback(*b, stores.backing);
    PrintJson(JsonOk("\"op\":\"r2-load-lazy-b\",\"posx_b\":" +
                     std::to_string(b->posx) + ",\"lazy_load_count\":" +
                     std::to_string(LazyLoadCount())));
    return 0;
  }

  if (op == "direct-load-refuse") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    ae::Ptr<PrefabWindow> a = ws->window_a.Load();
    if (!a->prefab_origin.is_valid() ||
        !PrefabPtr<PrefabWindow>::RegisterOrigin(a->obj_id, a->prefab_origin)) {
      PrintJson("{\"ok\":false,\"error\":\"expected-prefab-origin\"}");
      return 1;
    }
    if (!PrefabPtr<PrefabWindow>::RefuseDirectPrefabInstanceLoad(
            ae::ObjId{kWindowAId})) {
      PrintJson("{\"ok\":false,\"error\":\"refuse-policy\"}");
      return 1;
    }
    PrintJson(JsonOk("\"op\":\"direct-load-refuse\""));
    return 0;
  }

  if (op == "replay-check") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded);
    ae::Ptr<WorkspaceRoot> ws{loaded};
    ae::Ptr<PrefabWindow> a = ws->window_a.Load();
    std::int32_t const materialized_posx = a->posx;
    a->ReplayFromBase();
    std::int32_t const replay_posx = a->posx;
    a->ReplayFromBase();
    std::int32_t const replay_again = a->posx;
    ae::Ptr<PrefabWindow> base_window = a->base.Load();
    bool const base_origin_ok =
        base_window && base_window->prefab_origin == a->prefab_origin;
    PrintJson(JsonOk(
        "\"op\":\"replay-check\""
        ",\"materialized_posx\":" +
        std::to_string(materialized_posx) +
        ",\"replay_posx\":" + std::to_string(replay_posx) +
        ",\"replay_idempotent\":" +
        (replay_posx == replay_again ? "true" : "false") +
        ",\"base_prefab_origin_ok\":" + (base_origin_ok ? "true" : "false")));
    return base_origin_ok && materialized_posx == replay_posx ? 0 : 1;
  }

  PrintJson("{\"ok\":false,\"error\":\"unknown-op\"}");
  return 2;
}

}  // namespace apptraverse::test::prefab_release

#endif  // APPTRAVERSE_PREFAB_FIXTURE_IMPL
