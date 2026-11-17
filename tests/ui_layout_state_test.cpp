#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <set>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/ui/ui_layout_state.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::test {
namespace {

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':' \
                << __LINE__ << '\n';                                       \
      std::exit(1);                                                         \
    }                                                                       \
  } while (false)

using ui::UiLayoutAlignment;
using ui::UiLayoutDefinition;
using ui::UiLayoutFlow;
using ui::UiLayoutNode;
using ui::UiLayoutState;

ae::ObjId const kStateId{0xBC110001};
ae::ObjId const kDefinitionId{0xBC110002};

UiLayoutNode Definition(double padding) {
  UiLayoutNode left;
  left.control_id = 11;
  left.min_width = 40;
  left.min_height = 20;
  left.preferred_width = 120;
  left.preferred_height = 80;
  left.max_width = 600;
  left.max_height = 400;
  left.grow = 2;
  left.alignment = UiLayoutAlignment::kCenter;

  UiLayoutNode button;
  button.control_id = 12;
  button.min_width = 50;
  button.min_height = 24;
  button.preferred_width = 100;
  button.preferred_height = 32;
  button.grow = 0;

  UiLayoutNode actions;
  actions.flow = UiLayoutFlow::kVertical;
  actions.padding = 3;
  actions.gap = 7;
  actions.min_width = 70;
  actions.alignment = UiLayoutAlignment::kEnd;
  actions.children = {button};

  UiLayoutNode root;
  root.flow = UiLayoutFlow::kAdaptive;
  root.padding = padding;
  root.gap = 9;
  root.children = {left, actions};
  return root;
}

void CheckSame(UiLayoutNode const& actual, UiLayoutNode const& expected) {
  CHECK(actual.control_id == expected.control_id);
  CHECK(actual.flow == expected.flow);
  CHECK(actual.min_width == expected.min_width);
  CHECK(actual.min_height == expected.min_height);
  CHECK(actual.preferred_width == expected.preferred_width);
  CHECK(actual.preferred_height == expected.preferred_height);
  CHECK(actual.max_width == expected.max_width);
  CHECK(actual.max_height == expected.max_height);
  CHECK(actual.grow == expected.grow);
  if (actual.padding != expected.padding) {
    std::cerr << "layout padding actual=" << actual.padding
              << " expected=" << expected.padding << '\n';
  }
  CHECK(actual.padding == expected.padding);
  CHECK(actual.gap == expected.gap);
  CHECK(actual.alignment == expected.alignment);
  CHECK(actual.children.size() == expected.children.size());
  for (std::size_t i = 0; i < actual.children.size(); ++i) {
    CheckSame(actual.children[i], expected.children[i]);
  }
}

// Two calls build independent native distilled supplies. The definition identity
// stays stable while its immutable BACKING bytes change between revisions.
void DistillSupply(ae::RamDomainStorage& supply, UiLayoutNode const& tree) {
  ae::Domain domain{supply};
  auto definition = UiLayoutDefinition::ptr::Create(
      ae::CreateWith{domain}.with_id(kDefinitionId));
  definition->root = tree;
  auto state = UiLayoutState::ptr::Create(
      ae::CreateWith{domain}.with_id(kStateId));
  state->definition = definition;
  state->has_override = false;
  state->override_root = {};
  FinalizeDistilledGraph(*state);
  StabilizeDistilledPersistentSnapshot(*state);
  SaveDistilledRoot(*state);
}

UiLayoutState::ptr LoadState(ae::Domain& domain) {
  return LoadApplication<UiLayoutState>(domain, kStateId);
}

void SaveState(UiLayoutState& state) {
  ae::DomainGraph save{state.domain};
  auto held = UiLayoutState::ptr::MakeFromThis(&state);
  auto const result = save.SaveRoot(held.cached(), kStateId);
  CHECK(result.IsOk());
}

void CheckNoDefinitionInUser(ae::RamDomainStorage const& user) {
  auto const found = user.state.find(kDefinitionId);
  CHECK(found == user.state.end() || !found->second || found->second->empty());
}

void TestDistilledTreeRoundTrip() {
  auto const expected = Definition(13);
  ae::RamDomainStorage supply;
  DistillSupply(supply, expected);
  auto const supply_before = supply.state;

  // Reload both the supply bytes and graph, with no live producing Domain.
  std::vector<std::uint8_t> bytes;
  CHECK(SerializeUserStorage(supply, bytes));
  ae::RamDomainStorage cold_supply;
  CHECK(DeserializeUserStorage(cold_supply, bytes));
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, cold_supply, {kDefinitionId}};
  ae::Domain domain{overlay};
  auto state = LoadState(domain);
  CHECK(!state->has_override);
  CHECK(state->definition.id() == kDefinitionId);
  CheckSame(state->EffectiveRoot(), expected);
  SaveState(*state);
  CheckNoDefinitionInUser(user);
  CHECK(cold_supply.state == supply_before);
}

void TestRuntimeEventReplayAndColdPersistence() {
  ae::RamDomainStorage supply;
  DistillSupply(supply, Definition(13));
  auto const supply_before = supply.state;
  ae::RamDomainStorage user;
  auto const expected = Definition(31);
  {
    OverlayDomainStorage overlay{user, supply, {kDefinitionId}};
    ae::Domain domain{overlay};
    auto state = LoadState(domain);
    auto const generation = state->Generation();
    state->SetOverride(expected);
    CHECK(state->has_override);
    CHECK(state->Generation() > generation);
    CHECK(state->journal.size() == 1);
    CheckSame(state->EffectiveRoot(), expected);
    bool const replayed = state->TryReplayFromBase();
    CHECK(replayed);
    CheckSame(state->EffectiveRoot(), expected);
    SaveState(*state);
  }
  CheckNoDefinitionInUser(user);
  CHECK(supply.state == supply_before);

  auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto const profile = std::filesystem::temp_directory_path() /
      ("apptraverse_ui_layout_state_" + std::to_string(stamp));
  CHECK(SaveUserStorageFile(user, profile));
  ae::RamDomainStorage cold_user;
  CHECK(LoadUserStorageFile(cold_user, profile));
  {
    OverlayDomainStorage overlay{cold_user, supply, {kDefinitionId}};
    ae::Domain domain{overlay};
    auto state = LoadState(domain);
    CHECK(state->has_override);
    CHECK(state->journal.size() == 1);
    CheckSame(state->EffectiveRoot(), expected);
    bool const replayed = state->TryReplayFromBase();
    CHECK(replayed);
    CheckSame(state->EffectiveRoot(), expected);
  }
  CheckNoDefinitionInUser(cold_user);
  std::error_code error;
  std::filesystem::remove(profile / "user.bin", error);
  CHECK(!error);
  std::filesystem::remove(profile, error);
  CHECK(!error);
}

void TestCompatibleSupplyUpdateAndClearOverride() {
  ae::RamDomainStorage supply_a;
  ae::RamDomainStorage supply_b;
  auto const first = Definition(13);
  auto second = Definition(21);
  second.children[1].gap = 17;
  DistillSupply(supply_a, first);
  DistillSupply(supply_b, second);
  CHECK(supply_a.state != supply_b.state);
  auto const a_before = supply_a.state;
  auto const b_before = supply_b.state;

  ae::RamDomainStorage unchanged_user;
  {
    OverlayDomainStorage overlay{unchanged_user, supply_a, {kDefinitionId}};
    ae::Domain domain{overlay};
    auto state = LoadState(domain);
    CheckSame(state->EffectiveRoot(), first);
    SaveState(*state);
  }
  {
    OverlayDomainStorage overlay{unchanged_user, supply_b, {kDefinitionId}};
    ae::Domain domain{overlay};
    auto state = LoadState(domain);
    CHECK(!state->has_override);
    CheckSame(state->EffectiveRoot(), second);
  }

  ae::RamDomainStorage changed_user;
  auto const custom = Definition(47);
  {
    OverlayDomainStorage overlay{changed_user, supply_a, {kDefinitionId}};
    ae::Domain domain{overlay};
    auto state = LoadState(domain);
    state->SetOverride(custom);
    SaveState(*state);
  }
  {
    OverlayDomainStorage overlay{changed_user, supply_b, {kDefinitionId}};
    ae::Domain domain{overlay};
    auto state = LoadState(domain);
    CheckSame(state->EffectiveRoot(), custom);
    bool const replayed = state->TryReplayFromBase();
    CHECK(replayed);
    CheckSame(state->EffectiveRoot(), custom);
    state->ClearOverride();
    CHECK(!state->has_override);
    CHECK(state->journal.size() == 2);
    CheckSame(state->EffectiveRoot(), second);
    SaveState(*state);
  }
  {
    OverlayDomainStorage overlay{changed_user, supply_b, {kDefinitionId}};
    ae::Domain domain{overlay};
    auto state = LoadState(domain);
    CHECK(!state->has_override);
    CheckSame(state->EffectiveRoot(), second);
    bool const replayed = state->TryReplayFromBase();
    CHECK(replayed);
    CheckSame(state->EffectiveRoot(), second);
  }
  CheckNoDefinitionInUser(unchanged_user);
  CheckNoDefinitionInUser(changed_user);
  CHECK(supply_a.state == a_before);
  CHECK(supply_b.state == b_before);
}

void TestProxyAndUiPublication() {
  ae::RamDomainStorage supply;
  DistillSupply(supply, Definition(13));
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, supply, {kDefinitionId}};
  ae::Domain model_domain{overlay};
  auto model = LoadState(model_domain);
  std::deque<ModelObjectProxy::ModelWork> pending;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    pending.push_back(std::move(work));
  }};
  ByteSink initial;
  SerializeInitialPublication(*model, initial);
  ApplicationUiSession session{initial.bytes, nullptr, proxy};
  auto& mirror = static_cast<UiLayoutState&>(session.root());
  CHECK(mirror.domain != model->domain);
  CheckSame(mirror.EffectiveRoot(), Definition(13));

  auto const requested = Definition(37);
  proxy.Invoke(mirror.obj_id, &UiLayoutState::SetOverride, requested);
  CHECK(pending.size() == 1);
  CHECK(!model->has_override);
  CHECK(!mirror.has_override);
  auto work = std::move(pending.front());
  pending.pop_front();
  work(model_domain);
  CHECK(model->has_override);
  CHECK(!mirror.has_override);

  ByteSink update;
  SerializeStructuralNodePublication(*model, update);
  session.ApplyStructural(update.bytes);
  CheckSame(mirror.EffectiveRoot(), requested);
  CHECK(mirror.has_override);
  CHECK(model->journal.size() == 1);
  CHECK(mirror.journal.empty());
}

void TestInvalidOverrideDoesNotCommit() {
  ae::RamDomainStorage supply;
  auto const defaults = Definition(13);
  DistillSupply(supply, defaults);
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, supply, {kDefinitionId}};
  ae::Domain domain{overlay};
  auto state = LoadState(domain);
  auto const initial_generation = state->Generation();
  auto invalid = defaults;
  invalid.gap = -1;
  bool const accepted = state->TrySetOverride(invalid);
  CHECK(!accepted);
  CHECK(!state->has_override);
  CHECK(state->journal.empty());
  CHECK(state->Generation() == initial_generation);
  CheckSame(state->EffectiveRoot(), defaults);

  auto const custom = Definition(29);
  state->SetOverride(custom);
  auto const override_generation = state->Generation();
  invalid = custom;
  invalid.children[1].children[0].control_id = invalid.children[0].control_id;
  bool const duplicate_accepted = state->TrySetOverride(invalid);
  CHECK(!duplicate_accepted);
  CHECK(state->has_override);
  CHECK(state->journal.size() == 1);
  CHECK(state->Generation() == override_generation);
  CheckSame(state->EffectiveRoot(), custom);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  std::cout << "distilled tree round trip\n" << std::flush;
  apptraverse::test::TestDistilledTreeRoundTrip();
  std::cout << "runtime event replay and cold persistence\n" << std::flush;
  apptraverse::test::TestRuntimeEventReplayAndColdPersistence();
  std::cout << "supply update and ClearOverride\n" << std::flush;
  apptraverse::test::TestCompatibleSupplyUpdateAndClearOverride();
  std::cout << "proxy and UI publication\n" << std::flush;
  apptraverse::test::TestProxyAndUiPublication();
  std::cout << "invalid override rejection\n" << std::flush;
  apptraverse::test::TestInvalidOverrideDoesNotCommit();
  std::cout << "UI_LAYOUT_STATE ok distilled_tree event_replay cold_user "
               "supply_update clear_override proxy_publication atomic_validation\n";
  return 0;
}
