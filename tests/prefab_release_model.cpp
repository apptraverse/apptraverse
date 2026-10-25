#include "prefab_release_model.h"

#include <cstdlib>

#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
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
class SetWidthEvent;
class WindowCAddedEvent;

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

class PrefabLocalization : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::prefab_release::PrefabLocalization",
      PrefabLocalization, ae::Obj, 0)

 protected:
  PrefabLocalization() = default;

 public:
  explicit PrefabLocalization(ae::ObjProp prop) : Obj{prop} {}

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
#if APPTRAVERSE_PREFAB_EXE_REVISION >= 2
  APPTRAVERSE_OBJECT(PrefabWindow, Node, 1)
#else
  APPTRAVERSE_OBJECT(PrefabWindow, Node, 0)
#endif

 protected:
  PrefabWindow() = default;

 public:
  explicit PrefabWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(posx), AE_MMBR(user_owned_s0),
                    AE_MMBR(user_owned_s1))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    ++g_lazy_load_count;
    dnv(base_, width, user_owned_s0);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, width, user_owned_s0);
  }

#if APPTRAVERSE_PREFAB_EXE_REVISION >= 2
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
  void Apply(SetWidthEvent const& event);

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

class WorkspaceRoot : public NodeFor<WorkspaceRoot> {
  APPTRAVERSE_OBJECT(WorkspaceRoot, Node, 0)

 protected:
  WorkspaceRoot() = default;

 public:
  explicit WorkspaceRoot(ae::ObjProp prop) : NodeFor{prop} {}

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

  void Apply(WindowCAddedEvent const& event);

  PrefabPtr<PrefabWindow> window_a;
  PrefabPtr<PrefabWindow> window_b;
  PrefabPtr<PrefabWindow> window_c;
  ae::ObjPtr<NullPresenterNode> presenter;
  ae::ObjPtr<LayoutNode> layout;
  ae::ObjPtr<DetailsNode> details;
  ae::ObjPtr<PreferencesNode> preferences;
};

class WindowCAddedEvent : public EventFor<WorkspaceRoot, WindowCAddedEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::prefab_release::WindowCAddedEvent",
      WindowCAddedEvent, Event, 0)

 protected:
  WindowCAddedEvent() = default;

 public:
  explicit WindowCAddedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_c))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_c);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_c);
  }

  PrefabPtr<PrefabWindow> window_c;
};

void WorkspaceRoot::Apply(WindowCAddedEvent const& event) {
  window_c = event.window_c;
}

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

class SetWidthEvent : public EventFor<PrefabWindow, SetWidthEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::prefab_release::SetWidthEvent",
                           SetWidthEvent, Event, 0)

 protected:
  SetWidthEvent() = default;

 public:
  explicit SetWidthEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, width);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, width);
  }

  std::uint32_t width{0};
};

void PrefabWindow::Apply(SetPosxAbsoluteEvent const& event) {
  posx = event.posx;
  user_owned_s1 = true;
}

void PrefabWindow::Apply(SetPosxDeltaEvent const& event) {
  posx += event.delta;
  user_owned_s1 = true;
}

void PrefabWindow::Apply(SetWidthEvent const& event) {
  width = event.width;
  user_owned_s0 = true;
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
APPTRAVERSE_REGISTER(WindowCAddedEvent);
APPTRAVERSE_REGISTER(SetPosxAbsoluteEvent);
APPTRAVERSE_REGISTER(SetPosxDeltaEvent);
APPTRAVERSE_REGISTER(SetWidthEvent);

namespace {

struct BuiltPrefab {
  ae::ObjPtr<PrefabWindow> window;
  ae::ObjPtr<PrefabLocalization> localization;
  std::vector<ae::ObjId> const_string_ids;
};

BuiltPrefab BuildSinglePrefab(ae::Domain& domain, ae::ObjId prefab_id,
                              ae::ObjId prefab_base_id,
                              ae::ObjId localization_id,
                              PrefabDefaults const& defaults,
                              ae::ObjId label_const_id, char const* label) {
  BuiltPrefab out;
  auto base_ref = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(prefab_base_id));
  ae::Ptr<PrefabWindow> base = base_ref.Load();
  base->width = defaults.width;
  base->posx = defaults.posx;

  out.window = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(prefab_id));
  ae::Ptr<PrefabWindow> window = out.window.Load();
  window->width = defaults.width;
  window->posx = defaults.posx;
  window->base = base_ref;

  auto label_obj = ConstLabelString::ptr::Create(
      ae::CreateWith{domain}.with_id(label_const_id));
  label_obj.Load()->text = label;
  out.const_string_ids.push_back(label_const_id);

  out.localization = PrefabLocalization::ptr::Create(
      ae::CreateWith{domain}.with_id(localization_id));
  out.localization.Load()->strings.push_back(label_obj);

  FinalizeDistilledGraph(*window);
  StabilizeDistilledPersistentSnapshot(*window);
  SaveDistilledRoot(*window);
  out.localization.Save();

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
  (void)&g_apptraverse_registrar_WindowCAddedEvent;
  (void)&g_apptraverse_registrar_SetPosxAbsoluteEvent;
  (void)&g_apptraverse_registrar_SetPosxDeltaEvent;
  (void)&g_apptraverse_registrar_SetWidthEvent;
}

PrefabDefaults MainWindowPrefabDefaults() {
  return {.width = 640, .posx = 10};
}

PrefabDefaults ToolWindowPrefabDefaults() {
  return {.width = 480, .posx = 20};
}

ae::RamDomainStorage BuildPrefabSupplyGraph() {
  return BuildPrefabSupplyGraphWithMainPosx(MainWindowPrefabDefaults().posx);
}

ae::RamDomainStorage BuildPrefabSupplyGraphWithMainPosx(std::int32_t main_posx) {
  return BuildPrefabSupplyGraphWithMainTitleAndPosx(kMainTitleR0, main_posx);
}

ae::RamDomainStorage BuildPrefabSupplyGraphWithMainTitleAndPosx(
    char const* main_title, std::int32_t main_posx) {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  EnsurePrefabReleaseRegistration();

  PrefabDefaults main_defaults = MainWindowPrefabDefaults();
  main_defaults.posx = main_posx;
  (void)BuildSinglePrefab(domain, ae::ObjId{kMainWindowPrefabId},
                          ae::ObjId{kMainPrefabBaseId}, ae::ObjId{kMainLocalizationId},
                          main_defaults, ae::ObjId{kMainLabelConstId}, main_title);
  (void)BuildSinglePrefab(domain, ae::ObjId{kToolWindowPrefabId},
                          ae::ObjId{kToolPrefabBaseId},
                          ae::ObjId{kToolLocalizationId}, ToolWindowPrefabDefaults(),
                          ae::ObjId{kToolLabelConstId}, kToolTitle);

#if APPTRAVERSE_PREFAB_EXE_REVISION == 0
  StripPrefabWindowS1FromSupplyGraph(storage);
#endif

  return storage;
}

void StripPrefabWindowS1FromSupplyGraph(ae::RamDomainStorage& storage) {
  for (auto& [obj_id, class_map_opt] : storage.state) {
    if (!class_map_opt) {
      continue;
    }
    auto class_it = class_map_opt->find(PrefabWindow::kClassId);
    if (class_it != class_map_opt->end()) {
      class_it->second.erase(1);
      if (class_it->second.empty()) {
        class_map_opt->erase(class_it);
      }
    }
    if (class_map_opt->empty()) {
      storage.state.erase(obj_id);
    }
  }
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
  EnsurePrefabReleaseRegistration();

  WorkspaceRoot::ptr workspace = WorkspaceRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId{kWorkspaceId}));

  auto window_a_ref = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId{kWindowAId}));
  ae::Ptr<PrefabWindow> window_a = window_a_ref.Load();
  window_a->width = MainWindowPrefabDefaults().width;
  window_a->posx = MainWindowPrefabDefaults().posx;

  auto window_b_ref = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}
          .with_id(ae::ObjId{kWindowBId})
          .with_flags(ae::ObjFlags::kUnloadedByDefault));
  ae::Ptr<PrefabWindow> window_b = window_b_ref.Load();
  window_b->width = MainWindowPrefabDefaults().width;
  window_b->posx = MainWindowPrefabDefaults().posx;

  PrefabPtr<PrefabWindow> window_c_canonical;
  if (create_window_c) {
    auto window_c_ref = PrefabWindow::ptr::Create(
        ae::CreateWith{domain}.with_id(ae::ObjId{kWindowCId}));
    ae::Ptr<PrefabWindow> window_c = window_c_ref.Load();
    window_c->width = ToolWindowPrefabDefaults().width;
    window_c->posx = ToolWindowPrefabDefaults().posx;
    window_c_canonical = PrefabPtr<PrefabWindow>::FromInstance(
        window_c_ref, ae::ObjId{kToolWindowPrefabId});
  }

  auto presenter = NullPresenterNode::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId{kNullPresenterId}));
  presenter.Load()->localization = PrefabLocalization::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{kMainLocalizationId}));
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
  root->window_a = PrefabPtr<PrefabWindow>::FromInstance(
      window_a_ref, ae::ObjId{kMainWindowPrefabId});
  root->window_b = PrefabPtr<PrefabWindow>::FromInstance(
      window_b_ref, ae::ObjId{kMainWindowPrefabId});
  root->window_c = window_c_canonical;
  root->presenter = presenter;
  root->layout = layout;
  root->details = details;
  root->preferences = preferences;

  (void)plan;
  return ae::ObjPtr<ae::Obj>{workspace};
}

namespace {

bool UserHasLayerForInstance(DirectoryDomainStorage const& user,
                             ae::ObjId object_id, std::uint32_t class_id,
                             std::uint8_t version) {
  return DirectoryDomainStorageLayerFileExists(
      user.root(), ae::DomainQuery{object_id, class_id, version});
}

std::optional<std::int32_t> ReadPrefabPosxFromBacking(
    ae::RamDomainStorage const& backing, ae::ObjId prefab_id) {
#if APPTRAVERSE_PREFAB_EXE_REVISION >= 2
  ae::Domain backing_domain{const_cast<ae::RamDomainStorage&>(backing)};
  PrefabWindow::ptr ref = PrefabWindow::ptr::Declare(
      ae::CreateWith{backing_domain}.with_id(prefab_id));
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

}  // namespace

std::optional<std::uint32_t> ReadPrefabWidthFromBacking(
    ae::RamDomainStorage const& backing, ae::ObjId prefab_id) {
  ae::Domain backing_domain{const_cast<ae::RamDomainStorage&>(backing)};
  PrefabWindow::ptr prefab_ref = PrefabWindow::ptr::Declare(
      ae::CreateWith{backing_domain}.with_id(prefab_id));
  ae::Ptr<PrefabWindow> prefab = prefab_ref.Load();
  if (!prefab) {
    return std::nullopt;
  }
  return prefab->width;
}

CreatedToolWindowInstance CreateToolWindowInstanceFromBacking(
    ae::Domain& domain, ae::RamDomainStorage const& backing) {
  ae::ObjId const tool_prefab_id{ae::ObjId{kToolWindowPrefabId}};
  std::optional<std::uint32_t> const prefab_width =
      ReadPrefabWidthFromBacking(backing, tool_prefab_id);
  if (!prefab_width) {
    std::abort();
  }

  ae::ObjId const instance_id = ae::ObjId::GenerateUnique();
  ae::ObjId const instance_base_id = ae::ObjId::GenerateUnique();

  PrefabWindow::ptr base_ref = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_base_id));
  ae::Ptr<PrefabWindow> hist_base = base_ref.Load();
  hist_base->width = *prefab_width;

  PrefabWindow::ptr instance_ref = PrefabWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(instance_id));
  ae::Ptr<PrefabWindow> instance = instance_ref.Load();
  instance->width = *prefab_width;
  instance->base = base_ref;

  CreatedToolWindowInstance out;
  out.canonical = PrefabPtr<PrefabWindow>::FromInstance(instance_ref,
                                                        tool_prefab_id);
  out.instance_id = instance_id;
  out.instance_base_id = instance_base_id;
  out.width = *prefab_width;
  return out;
}

bool ValidateWorkspaceCanonicalPrefabPtrs(WorkspaceRoot const& ws,
                                          char const** error_out) {
  struct Slot {
    PrefabPtr<PrefabWindow> const* ptr;
    char const* name;
  };
  Slot const slots[] = {
      {&ws.window_a, "window_a"},
      {&ws.window_b, "window_b"},
      {&ws.window_c, "window_c"},
  };
  std::unordered_map<ae::ObjId::Type, ae::ObjId::Type> instance_to_prefab;
  for (Slot const& slot : slots) {
    if (!slot.ptr->is_valid()) {
      continue;
    }
    ae::ObjId const instance_id = slot.ptr->instance_id();
    ae::ObjId const prefab_id = slot.ptr->prefab_id();
    if (!instance_id.is_valid() || !prefab_id.is_valid()) {
      if (error_out != nullptr) {
        *error_out = slot.name;
      }
      return false;
    }
    auto [it, inserted] =
        instance_to_prefab.try_emplace(instance_id.id(), prefab_id.id());
    if (!inserted && it->second != prefab_id.id()) {
      if (error_out != nullptr) {
        *error_out = "duplicate_instance_conflicting_prefab";
      }
      return false;
    }
  }
  return true;
}

bool EnsurePrefabVersionInHistoricalBase(
    PrefabPtr<PrefabWindow> const& canonical, PrefabWindow& instance,
    ae::Domain& domain, DirectoryDomainStorage& user,
    ae::RamDomainStorage const& backing, std::uint8_t version) {
#if APPTRAVERSE_PREFAB_EXE_REVISION >= 2
  if (version != 1 || !instance.base.is_valid() ||
      !canonical.prefab_id().is_valid()) {
    return false;
  }
  ae::ObjId const base_id = instance.base.id();
  if (UserHasLayerForInstance(user, base_id, PrefabWindow::kClassId, version)) {
    return true;
  }
  ae::Domain backing_domain{const_cast<ae::RamDomainStorage&>(backing)};
  PrefabWindow::ptr prefab_ref = PrefabWindow::ptr::Declare(
      ae::CreateWith{backing_domain}.with_id(canonical.prefab_id()));
  ae::Ptr<PrefabWindow> prefab = prefab_ref.Load();
  if (!prefab) {
    return false;
  }
  std::optional<std::int32_t> default_posx =
      ReadPrefabPosxFromBacking(backing, canonical.prefab_id());
  if (prefab->base.is_valid()) {
    ae::Ptr<PrefabWindow> prefab_base{prefab->base.Load()};
    if (prefab_base) {
      default_posx = prefab_base->posx;
    }
  }
  if (!default_posx) {
    return false;
  }
  ae::Ptr<PrefabWindow> inst_base{instance.base.Load()};
  if (!inst_base) {
    return false;
  }
  inst_base->posx = *default_posx;
  inst_base->user_owned_s1 = false;
  user.BeginSaveSession();
  ae::DomainGraph graph{&domain};
  if (!graph.SaveVersion(ae::Version<1>{}, *inst_base, base_id).IsOk() ||
      user.SaveSessionHadFailure()) {
    return false;
  }
  return true;
#else
  (void)canonical;
  (void)instance;
  (void)domain;
  (void)user;
  (void)backing;
  (void)version;
  return false;
#endif
}

ae::Ptr<PrefabWindow> MaterializePrefabBackedWindow(
    PrefabPtr<PrefabWindow> const& canonical,
    PrefabBackedLoadContext const& ctx) {
  ae::Ptr<PrefabWindow> window = canonical.Load();
  if (!window || !canonical.prefab_id().is_valid()) {
    return {};
  }
#if APPTRAVERSE_PREFAB_EXE_REVISION >= 2
  if (!window->user_owned_s1 &&
      !UserHasLayerForInstance(ctx.user, canonical.instance_id(),
                               PrefabWindow::kClassId, 1)) {
    if (auto posx =
            ReadPrefabPosxFromBacking(ctx.backing, canonical.prefab_id())) {
      window->posx = *posx;
    }
  }
#else
  (void)ctx;
#endif
  return window;
}

std::uint32_t LazyLoadCount() { return g_lazy_load_count; }

void ResetLazyLoadCount() { g_lazy_load_count = 0; }

}  // namespace apptraverse::test::prefab_release

#if defined(APPTRAVERSE_PREFAB_FIXTURE_IMPL)

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distilled_supply_package.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"

#if APPTRAVERSE_PREFAB_EXE_REVISION == 0
#include "embedded_distilled_prefab_release_r0_supply.h"
#elif APPTRAVERSE_PREFAB_EXE_REVISION == 1
#include "embedded_distilled_prefab_release_r1_supply.h"
#else
#include "embedded_distilled_prefab_release_r2_supply.h"
#include "embedded_distilled_prefab_release_supply_v30.h"
#endif
#include "embedded_release_metadata.h"
#include "prefab_fixture_state.h"

namespace apptraverse::test::prefab_release {
namespace {

void PrintJson(std::string const& body) { std::cout << body << '\n'; }

std::string JsonOk(std::string_view fields) {
  return std::string{"{\"ok\":true,"} + std::string{fields} + "}";
}

enum class PrefabBackingKind { kDefault, kMainPosx30 };

struct FixtureBacking {
  ae::RamDomainStorage embedded_backing;
  ae::RamDomainStorage effective_backing;
  std::set<ae::ObjId> supply_ids;
  bool package_applied{false};
};

struct FixtureStorage {
  prefab_fixture::StateLayout layout;
  DirectoryDomainStorage user;
  FixtureBacking backing;
  OverlayDomainStorage overlay;
  ae::Domain domain;

  FixtureStorage(std::filesystem::path const& state_dir,
                 PrefabBackingKind kind,
                 std::filesystem::path const& explicit_package_path)
      : layout{prefab_fixture::ResolveStateLayout(state_dir)},
        user{layout.user_root},
        backing{BuildFixtureBacking(layout, kind, explicit_package_path)},
        overlay{user, backing.effective_backing, backing.supply_ids},
        domain{overlay} {}

  explicit FixtureStorage(std::filesystem::path const& state_dir,
                          PrefabBackingKind kind = PrefabBackingKind::kDefault)
      : FixtureStorage(state_dir, kind, {}) {}

 private:
  static apptraverse::EmbeddedDistilledGraph const& GraphFor(
      PrefabBackingKind kind) {
#if APPTRAVERSE_PREFAB_EXE_REVISION == 1
    (void)kind;
    return embedded_distilled::prefab_release_r1::kEmbeddedGraph;
#elif APPTRAVERSE_PREFAB_EXE_REVISION >= 2
    if (kind == PrefabBackingKind::kMainPosx30) {
      return embedded_distilled::prefab_release_v30::kEmbeddedGraph;
    }
    return embedded_distilled::prefab_release_r2::kEmbeddedGraph;
#else
    (void)kind;
    return embedded_distilled::prefab_release_r0::kEmbeddedGraph;
#endif
  }

  static std::set<ae::ObjId> SupplyIdSetFromPackage(
      DistilledSupplyPackage const& package) {
    std::set<ae::ObjId> ids;
    for (auto id : package.target_supply_constant_ids) {
      ids.insert(ae::ObjId{id});
    }
    return ids;
  }

  static void ApplyPackageFile(FixtureBacking& out,
                               std::filesystem::path const& package_path) {
    auto package = ReadDistilledSupplyPackageFile(package_path);
    if (!package) {
      throw std::runtime_error("supply-package");
    }
    ApplyDistilledSupplyPackage(out.effective_backing, *package);
    out.supply_ids = SupplyIdSetFromPackage(*package);
    out.package_applied = true;
  }

  static FixtureBacking BuildFixtureBacking(
      prefab_fixture::StateLayout const& layout, PrefabBackingKind kind,
      std::filesystem::path const& explicit_package_path) {
    FixtureBacking out{};
    InstallEmbeddedDistilledBacking(out.embedded_backing, GraphFor(kind));
    out.supply_ids = EmbeddedSupplyConstantIdSet(GraphFor(kind));
    out.effective_backing = out.embedded_backing;

    std::uint32_t const embedded =
        prefab_release_lifecycle::kEmbeddedRelease;

    if (!explicit_package_path.empty()) {
      ApplyPackageFile(out, explicit_package_path);
      return out;
    }

    if (layout.state_root.empty()) {
      return out;
    }

    std::uint32_t const active =
        prefab_fixture::ActiveReleaseForStateRoot(layout.state_root, embedded);
    if (active <= embedded) {
      return out;
    }

    for (std::uint32_t r = embedded + 1; r <= active; ++r) {
      auto const package_path =
          layout.state_root / std::to_string(r) / "supply.bin";
      if (!std::filesystem::exists(package_path)) {
        continue;
      }
      ApplyPackageFile(out, package_path);
    }
    return out;
  }
};

PrefabBackedLoadContext MakeLoadContext(FixtureStorage& stores) {
  return PrefabBackedLoadContext{stores.domain, stores.backing.effective_backing,
                                 stores.user};
}

ae::ObjId::Type PrefabBaseObjIdFromBacking(ae::RamDomainStorage const& backing,
                                           ae::ObjId prefab_id) {
  if (!prefab_id.is_valid()) {
    return 0;
  }
  ae::Domain backing_domain{const_cast<ae::RamDomainStorage&>(backing)};
  PrefabWindow::ptr prefab_ref = PrefabWindow::ptr::Declare(
      ae::CreateWith{backing_domain}.with_id(prefab_id));
  ae::Ptr<PrefabWindow> prefab = prefab_ref.Load();
  if (!prefab || !prefab->base.is_valid()) {
    return 0;
  }
  return prefab->base.id().id();
}

std::optional<std::int32_t> InstanceBasePosx(PrefabWindow const& instance) {
  if (!instance.base.is_valid()) {
    return std::nullopt;
  }
  ae::Ptr<PrefabWindow> base{instance.base.Load()};
  if (!base) {
    return std::nullopt;
  }
  return base->posx;
}

bool PrefabBaseChainOk(PrefabPtr<PrefabWindow> const& canonical,
                       PrefabWindow const& instance,
                       ae::RamDomainStorage const& backing) {
  if (!instance.base.is_valid() || !canonical.prefab_id().is_valid()) {
    return false;
  }
  ae::Domain backing_domain{const_cast<ae::RamDomainStorage&>(backing)};
  PrefabWindow::ptr prefab_ref = PrefabWindow::ptr::Declare(
      ae::CreateWith{backing_domain}.with_id(canonical.prefab_id()));
  ae::Ptr<PrefabWindow> prefab = prefab_ref.Load();
  if (!prefab || !prefab->base.is_valid()) {
    return false;
  }
  ae::Ptr<ae::Obj> inst_base_obj = instance.base.Load();
  ae::Ptr<ae::Obj> prefab_base_obj = prefab->base.Load();
  if (!inst_base_obj || !prefab_base_obj) {
    return false;
  }
  ae::Ptr<PrefabWindow> inst_base{inst_base_obj};
  ae::Ptr<PrefabWindow> prefab_base{prefab_base_obj};
  return static_cast<bool>(inst_base) && static_cast<bool>(prefab_base);
}

}  // namespace

std::optional<std::string> ReadLocalizationLabel(ae::Domain& domain,
                                                 ae::ObjId localization_id) {
  PrefabLocalization::ptr loc_ref = PrefabLocalization::ptr::Declare(
      ae::CreateWith{domain}.with_id(localization_id));
  ae::Ptr<PrefabLocalization> loc = loc_ref.Load();
  if (!loc || loc->strings.empty()) {
    return std::nullopt;
  }
  ae::Ptr<ConstLabelString> label{loc->strings[0].Load()};
  if (!label) {
    return std::nullopt;
  }
  return label->text;
}

int RunPrefabReleaseFixture(int argc, char** argv) {
  EnsurePrefabReleaseRegistration();

  std::filesystem::path state_dir;
  std::filesystem::path supply_package_path;
  std::optional<std::uint32_t> active_release_value;
  std::optional<std::uint32_t> width_value;
  std::string op;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--supply-package" && i + 1 < argc) {
      supply_package_path = argv[++i];
    } else if (arg == "--active-release" && i + 1 < argc) {
      active_release_value = static_cast<std::uint32_t>(std::stoul(argv[++i]));
    } else if (arg == "--width" && i + 1 < argc) {
      width_value = static_cast<std::uint32_t>(std::stoul(argv[++i]));
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

  auto make_stores = [&](PrefabBackingKind kind = PrefabBackingKind::kDefault) {
    return FixtureStorage{state_dir, kind, supply_package_path};
  };

  if (op == "set-active-release") {
    auto layout = prefab_fixture::ResolveStateLayout(state_dir);
    if (layout.state_root.empty() || !active_release_value) {
      PrintJson("{\"ok\":false,\"error\":\"usage\"}");
      return 2;
    }
    std::filesystem::create_directories(layout.state_root);
    if (!prefab_fixture::WriteActiveReleaseFile(
            layout.state_root / "active-release", *active_release_value)) {
      PrintJson("{\"ok\":false,\"error\":\"write-active-release\"}");
      return 1;
    }
    PrintJson(JsonOk("\"op\":\"set-active-release\",\"active_release\":" +
                     std::to_string(*active_release_value)));
    return 0;
  }

  if (op == "r0-read-labels") {
    try {
      FixtureStorage stores = make_stores();
      std::uint32_t const active = prefab_fixture::ActiveReleaseForStateRoot(
          stores.layout.state_root,
          prefab_release_lifecycle::kEmbeddedRelease);
      ae::DomainGraph graph{&stores.domain};
      ae::Ptr<ae::Obj> loaded;
      if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
        PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
        return 1;
      }
      ae::Ptr<WorkspaceRoot> ws{loaded};
      std::optional<std::string> main_label =
          ReadLocalizationLabel(stores.domain, ae::ObjId{kMainLocalizationId});
      std::optional<std::string> tool_label =
          ReadLocalizationLabel(stores.domain, ae::ObjId{kToolLocalizationId});
      if (!main_label || !tool_label) {
        PrintJson("{\"ok\":false,\"error\":\"missing-label\"}");
        return 1;
      }
      PrefabBackedLoadContext ctx = MakeLoadContext(stores);
      ae::Ptr<PrefabWindow> a =
          MaterializePrefabBackedWindow(ws->window_a, ctx);
      PrintJson(JsonOk(
          "\"op\":\"r0-read-labels\""
          ",\"main_label\":\"" +
          *main_label + "\",\"tool_label\":\"" + *tool_label +
          "\",\"posx_a\":" + std::to_string(a->posx) +
          ",\"package_applied\":" +
          (stores.backing.package_applied ? "true" : "false") +
          ",\"window_a_instance_id\":" +
          std::to_string(ws->window_a.instance_id().id()) +
          ",\"embedded_release\":" +
          std::to_string(prefab_release_lifecycle::kEmbeddedRelease) +
          ",\"active_release\":" + std::to_string(active)));
      return 0;
    } catch (std::exception const& ex) {
      PrintJson(std::string("{\"ok\":false,\"error\":\"package\",\"detail\":\"") +
                ex.what() + "\"}");
      return 3;
    }
  }

  if (op == "read-user-equivalence") {
    try {
      FixtureStorage stores = make_stores();
      ae::DomainGraph graph{&stores.domain};
      ae::Ptr<ae::Obj> loaded;
      if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
        PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
        return 1;
      }
      ae::Ptr<WorkspaceRoot> ws{loaded};
      std::optional<std::string> main_label =
          ReadLocalizationLabel(stores.domain, ae::ObjId{kMainLocalizationId});
      std::optional<std::string> tool_label =
          ReadLocalizationLabel(stores.domain, ae::ObjId{kToolLocalizationId});
      if (!main_label || !tool_label) {
        PrintJson("{\"ok\":false,\"error\":\"missing-label\"}");
        return 1;
      }
      bool const window_b_loaded_before = ws->window_b.is_loaded();
      PrefabBackedLoadContext ctx = MakeLoadContext(stores);
      ae::Ptr<PrefabWindow> a =
          MaterializePrefabBackedWindow(ws->window_a, ctx);
      ae::ObjId::Type const prefab_base_id = PrefabBaseObjIdFromBacking(
          stores.backing.effective_backing, ws->window_a.prefab_id());
      PrintJson(JsonOk(
          "\"op\":\"read-user-equivalence\""
          ",\"main_label\":\"" +
          *main_label + "\",\"tool_label\":\"" + *tool_label +
          "\",\"width_a\":" + std::to_string(a->width) +
          ",\"posx_a\":" + std::to_string(a->posx) +
          ",\"window_a_instance_id\":" +
          std::to_string(ws->window_a.instance_id().id()) +
          ",\"window_a_prefab_id\":" +
          std::to_string(ws->window_a.prefab_id().id()) +
          ",\"window_b_instance_id\":" +
          std::to_string(ws->window_b.instance_id().id()) +
          ",\"window_b_prefab_id\":" +
          std::to_string(ws->window_b.prefab_id().id()) +
          ",\"window_b_loaded\":" +
          (window_b_loaded_before ? "true" : "false") +
          ",\"journal_len_a\":" + std::to_string(a->journal.size()) +
          ",\"alias_ok\":" +
          (ws->preferences.Load()->window_a_alias.id() == ws->window_a.id()
               ? "true"
               : "false") +
          ",\"prefab_base_id\":" + std::to_string(prefab_base_id) +
          ",\"package_applied\":" +
          (stores.backing.package_applied ? "true" : "false") +
          ",\"embedded_release\":" +
          std::to_string(prefab_release_lifecycle::kEmbeddedRelease)));
      return 0;
    } catch (std::exception const& ex) {
      PrintJson(std::string("{\"ok\":false,\"error\":\"package\",\"detail\":\"") +
                ex.what() + "\"}");
      return 3;
    }
  }

  if (op == "r0-init" || op == "d1-r0-init" || op == "d1l-r0-init") {
    auto layout = prefab_fixture::ResolveStateLayout(state_dir);
    if (std::filesystem::exists(layout.user_root) &&
        (!std::filesystem::is_directory(layout.user_root) ||
         !std::filesystem::is_empty(layout.user_root))) {
      PrintJson("{\"ok\":false,\"error\":\"state-not-empty\"}");
      return 1;
    }
    std::filesystem::create_directories(layout.user_root);
    DirectoryDomainStorage user{layout.user_root};
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
    char const* graph_error = nullptr;
    if (!ValidateWorkspaceCanonicalPrefabPtrs(*ws, &graph_error)) {
      PrintJson("{\"ok\":false,\"error\":\"canonical-prefab-ptr\",\"detail\":\"" +
                std::string(graph_error ? graph_error : "?") + "\"}");
      return 1;
    }
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    bool const b_loaded_before = ws->window_b.is_loaded();
    PrintJson(JsonOk(
        "\"op\":\"r2-verify\""
        ",\"width_a\":" +
        std::to_string(a->width) +
        ",\"posx_a\":" + std::to_string(a->posx) +
        ",\"window_a_instance_id\":" +
        std::to_string(ws->window_a.instance_id().id()) +
        ",\"window_a_prefab_id\":" +
        std::to_string(ws->window_a.prefab_id().id()) +
        ",\"window_b_instance_id\":" +
        std::to_string(ws->window_b.instance_id().id()) +
        ",\"window_b_prefab_id\":" +
        std::to_string(ws->window_b.prefab_id().id()) +
        ",\"window_b_loaded\":" + (b_loaded_before ? "true" : "false") +
        ",\"lazy_load_count\":" + std::to_string(LazyLoadCount()) +
        ",\"alias_ok\":" +
        (ws->preferences.Load()->window_a_alias.id() == ws->window_a.id()
             ? "true"
             : "false")));
    return 0;
  }

  if (op == "r2-get-posx-a") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    bool const user_s1 = UserHasLayerForInstance(
        stores.user, ws->window_a.instance_id(), PrefabWindow::kClassId, 1);
    PrintJson(JsonOk(std::string("\"op\":\"r2-get-posx-a\",\"posx_a\":") +
                     std::to_string(a->posx) + ",\"user_s1_a\":" +
                     (user_s1 ? "true" : "false") + ",\"window_a_prefab_id\":" +
                     std::to_string(ws->window_a.prefab_id().id())));
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
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    if (delta) {
      if (!EnsurePrefabVersionInHistoricalBase(ws->window_a, *a, stores.domain,
                                               stores.user,
                                               stores.backing.effective_backing,
                                               1)) {
        PrintJson("{\"ok\":false,\"error\":\"ensure-base-s1\"}");
        return 1;
      }
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
    bool const user_s1 = UserHasLayerForInstance(
        stores.user, ws->window_a.instance_id(), PrefabWindow::kClassId, 1);
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
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> b = MaterializePrefabBackedWindow(ws->window_b, ctx);
    PrintJson(JsonOk("\"op\":\"r2-load-lazy-b\",\"posx_b\":" +
                     std::to_string(b->posx) + ",\"lazy_load_count\":" +
                     std::to_string(LazyLoadCount())));
    return 0;
  }

  if (op == "d1-r2-readonly-a") {
    ResetLazyLoadCount();
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    ae::ObjId const base_id = a->base.id();
    bool const user_s1_instance = UserHasLayerForInstance(
        stores.user, ws->window_a.instance_id(), PrefabWindow::kClassId, 1);
    bool const user_s1_base =
        base_id.is_valid() &&
        UserHasLayerForInstance(stores.user, base_id, PrefabWindow::kClassId, 1);
    PrintJson(JsonOk(
        "\"op\":\"d1-r2-readonly-a\""
        ",\"width_a\":" +
        std::to_string(a->width) + ",\"posx_a\":" +
        std::to_string(a->posx) + ",\"user_s1_instance_present\":" +
        (user_s1_instance ? "true" : "false") + ",\"user_s1_base_present\":" +
        (user_s1_base ? "true" : "false") + ",\"window_class_id\":" +
        std::to_string(PrefabWindow::kClassId) + ",\"instance_base_id\":" +
        std::to_string(base_id.id()) + ",\"lazy_load_count\":" +
        std::to_string(LazyLoadCount())));
    return 0;
  }

  if (op == "d1-r2-delta-a") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    if (!EnsurePrefabVersionInHistoricalBase(ws->window_a, *a, stores.domain,
                                             stores.user,
                                             stores.backing.effective_backing,
                                             1)) {
      PrintJson("{\"ok\":false,\"error\":\"ensure-base-s1\"}");
      return 1;
    }
    auto event = SetPosxDeltaEvent::ptr::Create(ae::CreateWith{stores.domain});
    event.Load()->delta = 5;
    a->Commit(event);
    stores.user.BeginSaveSession();
    ae::DomainGraph save_graph{&stores.domain};
    if (!save_graph.SaveRoot(loaded, ae::ObjId{kWorkspaceId}).IsOk() ||
        stores.user.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    PrintJson(JsonOk("\"op\":\"d1-r2-delta-a\",\"posx_a\":" +
                     std::to_string(a->posx)));
    return 0;
  }

  if (op == "d1-r2-inspect-a") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded);
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    ae::ObjId const base_id = a->base.id();
    std::optional<std::int32_t> base_posx = InstanceBasePosx(*a);
    bool const user_s1_instance = UserHasLayerForInstance(
        stores.user, ws->window_a.instance_id(), PrefabWindow::kClassId, 1);
    bool const user_s1_base =
        base_id.is_valid() &&
        UserHasLayerForInstance(stores.user, base_id, PrefabWindow::kClassId, 1);
    ae::ObjId::Type const prefab_base_id = PrefabBaseObjIdFromBacking(
        stores.backing.effective_backing, ws->window_a.prefab_id());
    PrintJson(JsonOk(
        std::string("\"op\":\"d1-r2-inspect-a\",\"width_a\":") +
        std::to_string(a->width) + ",\"posx_a\":" +
        std::to_string(a->posx) + ",\"base_posx\":" +
        std::to_string(base_posx.value_or(-1)) + ",\"journal_len\":" +
        std::to_string(a->journal.size()) + ",\"user_s1_instance\":" +
        (user_s1_instance ? "true" : "false") + ",\"user_s1_base\":" +
        (user_s1_base ? "true" : "false") + ",\"instance_base_id\":" +
        std::to_string(base_id.id()) + ",\"prefab_base_id\":" +
        std::to_string(prefab_base_id) + ",\"window_class_id\":" +
        std::to_string(PrefabWindow::kClassId)));
    return 0;
  }

  if (op == "d1-r2-replay-a" || op == "d1-r2-replay-a-fault" ||
      op == "d1-r2-replay-a-v30") {
    bool const fault = op == "d1-r2-replay-a-fault";
    PrefabBackingKind const backing_kind =
        op == "d1-r2-replay-a-v30" ? PrefabBackingKind::kMainPosx30
                                   : PrefabBackingKind::kDefault;
    FixtureStorage stores{state_dir, backing_kind};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded);
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    std::int32_t const materialized = a->posx;
    std::uint32_t const materialized_width = a->width;
    if (fault) {
      a->posx = 999;
    }
    a->ReplayFromBase();
    std::int32_t const replay_once = a->posx;
    std::uint32_t const replay_width_once = a->width;
    a->ReplayFromBase();
    std::int32_t const replay_twice = a->posx;
    std::uint32_t const replay_width_twice = a->width;
    PrintJson(JsonOk(
        "\"op\":\"" + op + "\",\"materialized_posx\":" +
        std::to_string(materialized) + ",\"replay_posx\":" +
        std::to_string(replay_once) + ",\"replay_twice\":" +
        std::to_string(replay_twice) + ",\"materialized_width\":" +
        std::to_string(materialized_width) + ",\"replay_width\":" +
        std::to_string(replay_width_once) + ",\"replay_width_twice\":" +
        std::to_string(replay_width_twice)));
    return 0;
  }

  if (op == "set-width-a") {
    if (!width_value) {
      PrintJson("{\"ok\":false,\"error\":\"usage\"}");
      return 2;
    }
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    auto event = SetWidthEvent::ptr::Create(ae::CreateWith{stores.domain});
    event.Load()->width = *width_value;
    a->Commit(event);
    stores.user.BeginSaveSession();
    ae::DomainGraph save_graph{&stores.domain};
    if (!save_graph.SaveRoot(loaded, ae::ObjId{kWorkspaceId}).IsOk() ||
        stores.user.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    PrintJson(JsonOk("\"op\":\"set-width-a\",\"width_a\":" +
                     std::to_string(a->width)));
    return 0;
  }

  if (op == "create-tool-window") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    if (ws->window_c.is_valid()) {
      PrintJson("{\"ok\":false,\"error\":\"window-c-exists\"}");
      return 1;
    }
    CreatedToolWindowInstance created = CreateToolWindowInstanceFromBacking(
        stores.domain, stores.backing.effective_backing);
    auto event =
        WindowCAddedEvent::ptr::Create(ae::CreateWith{stores.domain});
    event.Load()->window_c = created.canonical;
    ws->Commit(event);
    FinalizeDistilledGraph(*ws);
    stores.user.BeginSaveSession();
    ae::DomainGraph save_graph{&stores.domain};
    if (!save_graph.SaveRoot(loaded, ae::ObjId{kWorkspaceId}).IsOk() ||
        stores.user.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    PrintJson(JsonOk(
        "\"op\":\"create-tool-window\""
        ",\"window_c_instance_id\":" +
        std::to_string(created.instance_id.id()) +
        ",\"window_c_instance_base_id\":" +
        std::to_string(created.instance_base_id.id()) +
        ",\"window_c_prefab_id\":" +
        std::to_string(kToolWindowPrefabId) + ",\"width_c\":" +
        std::to_string(created.width)));
    return 0;
  }

  if (op == "inspect-window-c") {
    ResetLazyLoadCount();
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    if (!ws->window_c.is_valid()) {
      PrintJson("{\"ok\":false,\"error\":\"missing-window-c\"}");
      return 1;
    }
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a =
        MaterializePrefabBackedWindow(ws->window_a, ctx);
    ae::Ptr<PrefabWindow> c =
        MaterializePrefabBackedWindow(ws->window_c, ctx);
    ae::ObjId const c_base_id = c->base.id();
    bool const user_s1_c = UserHasLayerForInstance(
        stores.user, ws->window_c.instance_id(), PrefabWindow::kClassId, 1);
    bool const user_s1_c_base =
        c_base_id.is_valid() &&
        UserHasLayerForInstance(stores.user, c_base_id, PrefabWindow::kClassId,
                                1);
    bool const b_loaded_before = ws->window_b.is_loaded();
    std::optional<std::int32_t> base_posx_c = InstanceBasePosx(*c);
    PrintJson(JsonOk(
        "\"op\":\"inspect-window-c\""
        ",\"window_c_instance_id\":" +
        std::to_string(ws->window_c.instance_id().id()) +
        ",\"window_c_prefab_id\":" +
        std::to_string(ws->window_c.prefab_id().id()) +
        ",\"width_c\":" + std::to_string(c->width) +
        ",\"posx_c\":" + std::to_string(c->posx) +
        ",\"base_posx_c\":" + std::to_string(base_posx_c.value_or(-1)) +
        ",\"journal_len_c\":" + std::to_string(c->journal.size()) +
        ",\"instance_base_id\":" + std::to_string(c_base_id.id()) +
        ",\"user_s1_instance\":" + (user_s1_c ? "true" : "false") +
        ",\"user_s1_base\":" + (user_s1_c_base ? "true" : "false") +
        ",\"window_a_instance_id\":" +
        std::to_string(ws->window_a.instance_id().id()) +
        ",\"window_a_prefab_id\":" +
        std::to_string(ws->window_a.prefab_id().id()) +
        ",\"posx_a\":" + std::to_string(a->posx) +
        ",\"width_a\":" + std::to_string(a->width) +
        ",\"window_b_loaded\":" + (b_loaded_before ? "true" : "false") +
        ",\"alias_ok\":" +
        (ws->preferences.Load()->window_a_alias.id() == ws->window_a.id()
             ? "true"
             : "false") +
        ",\"window_class_id\":" + std::to_string(PrefabWindow::kClassId) +
        ",\"lazy_load_count\":" + std::to_string(LazyLoadCount())));
    return 0;
  }

  if (op == "delta-posx-c") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    if (!ws->window_c.is_valid()) {
      PrintJson("{\"ok\":false,\"error\":\"missing-window-c\"}");
      return 1;
    }
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> c =
        MaterializePrefabBackedWindow(ws->window_c, ctx);
    if (!EnsurePrefabVersionInHistoricalBase(ws->window_c, *c, stores.domain,
                                             stores.user,
                                             stores.backing.effective_backing,
                                             1)) {
      PrintJson("{\"ok\":false,\"error\":\"ensure-base-s1\"}");
      return 1;
    }
    auto event = SetPosxDeltaEvent::ptr::Create(ae::CreateWith{stores.domain});
    event.Load()->delta = 7;
    c->Commit(event);
    stores.user.BeginSaveSession();
    ae::DomainGraph save_graph{&stores.domain};
    ae::ObjId const window_c_id = ws->window_c.instance_id();
    if (!save_graph.SaveRoot(c, window_c_id).IsOk() ||
        stores.user.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    PrintJson(JsonOk("\"op\":\"delta-posx-c\",\"posx_c\":" +
                     std::to_string(c->posx)));
    return 0;
  }

  if (op == "replay-window-c") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    ae::Ptr<WorkspaceRoot> ws{loaded};
    if (!ws->window_c.is_valid()) {
      PrintJson("{\"ok\":false,\"error\":\"missing-window-c\"}");
      return 1;
    }
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> c =
        MaterializePrefabBackedWindow(ws->window_c, ctx);
    std::uint32_t const materialized_width = c->width;
    std::int32_t const materialized_posx = c->posx;
    c->ReplayFromBase();
    std::uint32_t const replay_width = c->width;
    std::int32_t const replay_posx = c->posx;
    c->ReplayFromBase();
    std::uint32_t const replay_width_twice = c->width;
    std::int32_t const replay_posx_twice = c->posx;
    PrintJson(JsonOk(
        "\"op\":\"replay-window-c\""
        ",\"materialized_width\":" +
        std::to_string(materialized_width) + ",\"materialized_posx\":" +
        std::to_string(materialized_posx) + ",\"replay_width\":" +
        std::to_string(replay_width) + ",\"replay_posx\":" +
        std::to_string(replay_posx) + ",\"replay_width_twice\":" +
        std::to_string(replay_width_twice) + ",\"replay_posx_twice\":" +
        std::to_string(replay_posx_twice)));
    return 0;
  }

  if (op == "touch-save-workspace") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded).IsOk()) {
      PrintJson("{\"ok\":false,\"error\":\"load-root\"}");
      return 1;
    }
    stores.user.BeginSaveSession();
    ae::DomainGraph save_graph{&stores.domain};
    if (!save_graph.SaveRoot(loaded, ae::ObjId{kWorkspaceId}).IsOk() ||
        stores.user.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    PrintJson(JsonOk("\"op\":\"touch-save-workspace\""));
    return 0;
  }

  if (op == "d1l-r2-load-ws") {
    ResetLazyLoadCount();
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded);
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrintJson(JsonOk(
        "\"op\":\"d1l-r2-load-ws\",\"window_b_loaded\":" +
        std::string(ws->window_b.is_loaded() ? "true" : "false") +
        ",\"lazy_load_count\":" + std::to_string(LazyLoadCount())));
    return ws->window_b.is_loaded() ? 1 : 0;
  }

  if (op == "d1l-r2-delta-b") {
    ResetLazyLoadCount();
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded);
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> b = MaterializePrefabBackedWindow(ws->window_b, ctx);
    if (!EnsurePrefabVersionInHistoricalBase(ws->window_b, *b, stores.domain,
                                             stores.user,
                                             stores.backing.effective_backing,
                                             1)) {
      PrintJson("{\"ok\":false,\"error\":\"ensure-base-s1\"}");
      return 1;
    }
    auto event = SetPosxDeltaEvent::ptr::Create(ae::CreateWith{stores.domain});
    event.Load()->delta = 7;
    b->Commit(event);
    stores.user.BeginSaveSession();
    ae::DomainGraph save_graph{&stores.domain};
    if (!save_graph.SaveRoot(loaded, ae::ObjId{kWorkspaceId}).IsOk() ||
        stores.user.SaveSessionHadFailure()) {
      PrintJson("{\"ok\":false,\"error\":\"save-root\"}");
      return 1;
    }
    PrintJson(JsonOk(
        std::string("\"op\":\"d1l-r2-delta-b\",\"posx_b\":") +
        std::to_string(b->posx) + ",\"lazy_load_count\":" +
        std::to_string(LazyLoadCount())));
    return 0;
  }

  if (op == "d1l-r2-replay-b") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded);
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> b = MaterializePrefabBackedWindow(ws->window_b, ctx);
    std::int32_t const materialized = b->posx;
    b->ReplayFromBase();
    std::int32_t const replay = b->posx;
    b->ReplayFromBase();
    PrintJson(JsonOk(
        "\"op\":\"d1l-r2-replay-b\",\"materialized_posx\":" +
        std::to_string(materialized) + ",\"replay_posx\":" +
        std::to_string(replay)));
    return 0;
  }

  if (op == "replay-check") {
    FixtureStorage stores{state_dir};
    ae::DomainGraph graph{&stores.domain};
    ae::Ptr<ae::Obj> loaded;
    graph.LoadRoot(ae::ObjId{kWorkspaceId}, loaded);
    ae::Ptr<WorkspaceRoot> ws{loaded};
    PrefabBackedLoadContext ctx = MakeLoadContext(stores);
    ae::Ptr<PrefabWindow> a = MaterializePrefabBackedWindow(ws->window_a, ctx);
    std::int32_t const materialized_posx = a->posx;
    a->ReplayFromBase();
    std::int32_t const replay_posx = a->posx;
    a->ReplayFromBase();
    std::int32_t const replay_again = a->posx;
    bool const base_prefab_chain_ok =
        PrefabBaseChainOk(ws->window_a, *a, stores.backing.effective_backing);
    PrintJson(JsonOk(
        "\"op\":\"replay-check\""
        ",\"materialized_posx\":" +
        std::to_string(materialized_posx) +
        ",\"replay_posx\":" + std::to_string(replay_posx) +
        ",\"replay_idempotent\":" +
        (replay_posx == replay_again ? "true" : "false") +
        ",\"base_prefab_chain_ok\":" +
        (base_prefab_chain_ok ? "true" : "false") +
        ",\"window_a_prefab_id\":" +
        std::to_string(ws->window_a.prefab_id().id())));
    return base_prefab_chain_ok && materialized_posx == replay_posx ? 0 : 1;
  }

  PrintJson("{\"ok\":false,\"error\":\"unknown-op\"}");
  return 2;
}

}  // namespace apptraverse::test::prefab_release

#endif  // APPTRAVERSE_PREFAB_FIXTURE_IMPL
