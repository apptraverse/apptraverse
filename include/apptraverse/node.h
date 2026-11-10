#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event_record.h"
#include "apptraverse/event_record_publication_seri.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/publication_serialization_scope.h"

#include "apptraverse/object_clone_hydrate.inl"

namespace apptraverse {

template <typename ConcreteNode>
bool BuildRemappedHistoricalBaseScratch(
    ConcreteNode& owner, ae::ObjId base_id,
    ae::RamDomainStorage const& captured_scratch,
    ae::RamDomainStorage& remapped_out,
    std::set<ae::ObjId>* live_reference_ids_out = nullptr,
    ae::DomainGraph::CloneSaveRemapTable* capture_leaf_remap_out = nullptr,
    std::set<ae::ObjId> const* owner_live_reference_ids = nullptr);

template <typename ConcreteNode>
bool InjectRemappedHistoricalBaseSnapshot(
    ConcreteNode& owner, ae::ObjId base_id,
    ae::RamDomainStorage const& captured_scratch,
    ae::IDomainStorage& persistent_storage);

namespace detail {

template <typename T>
bool CanCompactStoredObjectVersions(ae::IDomainStorage& storage,
                                    ae::ObjId object_id) {
  for (unsigned version =
           static_cast<unsigned>(T::kCurrentVersion.value) + 1U;
       version <= static_cast<unsigned>(std::numeric_limits<std::uint8_t>::max());
       ++version) {
    ae::DomainLoad const load = storage.Load(ae::DomainQuery{
        object_id, T::kClassId, static_cast<std::uint8_t>(version)});
    if (load.result == ae::DomainLoadResult::kLoaded ||
        load.result == ae::DomainLoadResult::kRemoved) {
      return false;
    }
  }
  if constexpr (std::is_same_v<T, ae::Obj>) {
    return true;
  } else {
    return CanCompactStoredObjectVersions<typename T::Base>(storage, object_id);
  }
}

}  // namespace detail

inline std::uint64_t SystemUtcMicros() {
  using Clock = std::chrono::system_clock;
  auto const now = Clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

class Node : public ae::Obj {
  // Native base-class serialization uses separate class layers.
  APPTRAVERSE_OBJECT(Node, ae::Obj, 0)

 protected:
  Node() : materialized_change_ctx_{nullptr}, materialized_change_fn_{nullptr}, generation_{1}, applied_journal_size_{kJournalFullyMaterialized}, journal_retention_policy_{}, journal_compaction_blocked_{false}, suppress_materialized_change_{false} {}

 public:
  explicit Node(ae::ObjProp prop) : Obj{prop}, materialized_change_ctx_{nullptr}, materialized_change_fn_{nullptr}, generation_{1}, applied_journal_size_{kJournalFullyMaterialized}, journal_retention_policy_{}, journal_compaction_blocked_{false}, suppress_materialized_change_{false} {}

  struct RebuildOperationScratch {};
  struct CompactionOperationScratch {};

  AE_OBJECT_REFLECT(AE_MMBR(base), AE_MMBR(journal))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, base, journal);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    if (detail::OmitNodeHistoryForPublication(dnv)) {
      detail::NoteNodePublicationOmitSave();
      SharedPtr<Node> empty_base{};
      static std::vector<EventRecord> const kEmptyJournal{};
      dnv(base_, empty_base, kEmptyJournal);
      return;
    }
    if (detail::OmitNodeHistoryForCanonicalClone(dnv)) {
      SharedPtr<Node> empty_base{};
      static std::vector<EventRecord> const kEmptyJournal{};
      dnv(base_, empty_base, kEmptyJournal);
      return;
    }
    if (detail::OmitNodeHistoryForHistoricalBaseSnapshot(dnv)) {
      SharedPtr<Node> empty_base{};
      static std::vector<EventRecord> const kEmptyJournal{};
      dnv(base_, empty_base, kEmptyJournal);
      return;
    }
    detail::NoteNodeHistorySerialized(journal.size(), base.is_valid());
    ae::DomainGraph* const graph =
        detail::DnvSerializationScope<std::decay_t<Dnv>>::GetGraph(dnv);
    bool const force_historical_base =
        graph != nullptr &&
        graph->serialization_scope ==
            ae::GraphSerializationScope::LocalPersistent &&
        !journal.empty() && base.is_valid();
    dnv(base_);
    bool saved_force_materialize = false;
    if (force_historical_base) {
      saved_force_materialize = graph->force_materialize;
      graph->force_materialize = true;
    }
    dnv(base);
    if (force_historical_base) {
      graph->force_materialize = saved_force_materialize;
    }
    dnv(journal);
  }

  SharedPtr<Node> base;
  std::vector<EventRecord> journal;

  std::uint64_t Generation() const { return generation_; }

  void AdoptPublishedGeneration(std::uint64_t generation) {
    generation_ = generation;
  }

  void SetJournalRetentionPolicy(JournalRetentionPolicy policy) {
    journal_retention_policy_ = policy;
  }

  JournalRetentionPolicy const& GetJournalRetentionPolicy() const {
    return journal_retention_policy_;
  }

  // Retention safety hold independent of count/age policy (e.g. tests).
  void SetJournalCompactionBlocked(bool blocked) {
    journal_compaction_blocked_ = blocked;
  }

  bool IsJournalCompactionBlocked() const {
    return journal_compaction_blocked_;
  }

  // Fold a safe contiguous prefix into base. Housekeeping only: generation and
  // materialized fields are unchanged; notifier is not invoked.
  void CompactJournal(std::uint64_t now_us) { CompactJournalImpl(now_us); }

  // Runtime-only, not reflected. Instance-scoped: each model runtime binds the
  // Nodes it owns. No process-global / thread_local / singleton notifier.
  using MaterializedChangeFn = void (*)(void* ctx, Node& node);

  void BindMaterializedChangeNotifier(void* ctx, MaterializedChangeFn fn) {
    materialized_change_ctx_ = ctx;
    materialized_change_fn_ = fn;
  }

  void ClearMaterializedChangeNotifier() {
    materialized_change_ctx_ = nullptr;
    materialized_change_fn_ = nullptr;
  }

  void CopyMaterializedChangeNotifierFrom(Node const& source);

  bool HasMaterializedChangeNotifier() const {
    return materialized_change_fn_ != nullptr;
  }

  void* MaterializedChangeContext() const { return materialized_change_ctx_; }
  MaterializedChangeFn MaterializedChangeCallback() const {
    return materialized_change_fn_;
  }

  virtual void OnLoad() {}

  virtual void Update(std::chrono::steady_clock::time_point now) {
    (void)now;
  }

  bool TryEnsureCurrentGeneration() {
    if (applied_journal_size_ == kJournalFullyMaterialized) {
      applied_journal_size_ = journal.size();
      return true;
    }
    while (applied_journal_size_ < journal.size()) {
      auto const& record = journal[applied_journal_size_];
      if (!record.event.is_valid() || !record.event.is_loaded()) {
        return false;
      }
      if (!record.event->CanApplyTo(*this)) {
        return false;
      }
      ApplyEvent(*record.event);
      ++applied_journal_size_;
    }
    return true;
  }

  void EnsureCurrentGeneration() {
    bool const ok = TryEnsureCurrentGeneration();
    assert(ok && "EnsureCurrentGeneration invariant broken");
    (void)ok;
  }

  void CaptureBaseState() { CaptureBaseStateImpl(); }

  void Commit(Event::ptr event) { CommitImpl(std::move(event)); }

  // Rebuild materialized state from base and replay the journal.
  // Note: Speculative replay is intended for disposable scratch copies only.
  // If historical replay fails, the scratch object is left partially modified
  // and must not be reused.
  bool TryReplayFromBase() { return TryReplayFromBaseImpl(); }

  void ReplayFromBase() {
    bool const ok = TryReplayFromBase();
    assert(ok && "ReplayFromBase invariant broken");
    (void)ok;
  }

  // Ensure lazy mutable referents exist in the HistoricalBaseSnapshot closure.
  virtual void PrepareReachableMutableLeavesForHistoricalCapture() {}

  // ObjIds that must keep live domain identity inside a historical base snapshot
  // (for example overlay supply constants referenced from the owner).
  virtual void CollectHistoricalCaptureLiveReferenceIds(
      std::set<ae::ObjId>& out) const {
    (void)out;
  }

 protected:
  void ApplyEvent(Event const& event) { event.ApplyTo(*this); }

  void NoteMaterializedChange() {
    if (suppress_materialized_change_) {
      return;
    }
    ++generation_;
    if (materialized_change_fn_ != nullptr) {
      materialized_change_fn_(materialized_change_ctx_, *this);
    }
  }

  bool TryReplayJournal() {
    applied_journal_size_ = 0;
    return TryEnsureCurrentGeneration();
  }

  void ReplayJournal() {
    bool const ok = TryReplayJournal();
    assert(ok && "ReplayJournal invariant broken");
    (void)ok;
  }

  // Local-persistent fields must survive mid-journal RebuildFromBaseAndReplay.
  // Base snapshots may predate those fields; Load from base must not discard
  // the live local state.
  virtual void StashLocalPersistentAcrossRebuild(void* rebuild_scratch) {
    (void)rebuild_scratch;
  }
  virtual void RestoreLocalPersistentAcrossRebuild(void* rebuild_scratch) {
    (void)rebuild_scratch;
  }

  // Fold journal[0, collapsed_count) into persisted layers of reachable
  // objects so HistoricalBaseSnapshot capture includes lazy side effects.
  virtual void FoldCollapsedJournalPrefixIntoReachableStorage(
      std::size_t collapsed_count, void* compaction_scratch) {
    (void)collapsed_count;
    (void)compaction_scratch;
  }

  virtual void ReleaseFoldCaptureMaterialization(void* compaction_scratch) {
    (void)compaction_scratch;
  }

  virtual void StashLiveReachableLayersBeforeHistoricalCapture(
      void* compaction_scratch) {
    (void)compaction_scratch;
  }

  virtual void RestoreLiveReachableLayersAfterHistoricalCapture(
      void* compaction_scratch) {
    (void)compaction_scratch;
  }

  template <typename ConcreteNode>
  bool TryRebuildFromBaseAndReplay(ConcreteNode& target) {
    if (domain == nullptr || !base.is_valid() || !base.is_loaded() ||
        base.id() == obj_id) {
      return false;
    }
    auto owner_id = obj_id;
    auto saved_base = base;
    auto saved_journal = journal;
    typename ConcreteNode::RebuildOperationScratch rebuild_scratch{};
    target.StashLocalPersistentAcrossRebuild(&rebuild_scratch);
    ae::Ptr<ae::Obj> const base_obj = saved_base.Load();
    if (!base_obj) {
      obj_id = owner_id;
      base = saved_base;
      journal = std::move(saved_journal);
      target.RestoreLocalPersistentAcrossRebuild(&rebuild_scratch);
      return false;
    }
    ae::RamDomainStorage base_layers;
    {
      ae::Domain layer_domain{base_layers};
      ae::DomainGraph save_graph{
          &layer_domain, ae::GraphSerializationScope::LocalPersistent};
      save_graph.force_materialize = true;
      auto& base_node = static_cast<ConcreteNode&>(*base_obj);
      auto const save_result = save_graph.Save(base_node, saved_base.id());
      if (!save_result.has_value() || !save_result->IsOk()) {
        obj_id = owner_id;
        base = saved_base;
        journal = std::move(saved_journal);
        target.RestoreLocalPersistentAcrossRebuild(&rebuild_scratch);
        return false;
      }
    }
    if (!HydrateLiveObjectFromLayerScratch(
            *domain, target, saved_base.id(), base_layers, {}, {}, false,
            nullptr)) {
      obj_id = owner_id;
      base = saved_base;
      journal = std::move(saved_journal);
      target.RestoreLocalPersistentAcrossRebuild(&rebuild_scratch);
      return false;
    }
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    target.RestoreLocalPersistentAcrossRebuild(&rebuild_scratch);
    generation_ = 1;
    return TryReplayJournal();
  }

  template <typename ConcreteNode>
  void RebuildFromBaseAndReplay(ConcreteNode& target) {
    bool const ok = TryRebuildFromBaseAndReplay(target);
    assert(ok && "RebuildFromBaseAndReplay invariant broken");
    (void)ok;
  }

  template <typename ConcreteNode>
  void CaptureBaseStateInto(ConcreteNode& target) {
    assert(domain != nullptr);
    assert(base.is_valid());
    assert(base.is_loaded());
    assert(journal.empty());
    assert(base.id() != obj_id);

    auto owner_id = obj_id;
    auto saved_base = base;
    auto saved_journal = journal;

    base = Node::ptr{};
    journal.clear();

    target.PrepareReachableMutableLeavesForHistoricalCapture();
    std::set<ae::ObjId> owner_live_refs;
    target.CollectHistoricalCaptureLiveReferenceIds(owner_live_refs);

    ae::RamDomainStorage scratch;
    ae::Domain scratch_domain{scratch};
    ae::DomainGraph save_graph{
        &scratch_domain,
        ae::GraphSerializationScope::HistoricalBaseSnapshot};
    save_graph.force_materialize = true;
    bool const owner_marked =
        save_graph.cycle_detector.Add(ConcreteNode::kClassId, owner_id);
    assert(owner_marked);
    (void)owner_marked;
    auto const capture_save = save_graph.Save(target, saved_base.id());
    assert(capture_save.has_value() && capture_save->IsOk());

    ae::RamDomainStorage historical_layers;
    std::set<ae::ObjId> live_references;
    ae::DomainGraph::CloneSaveRemapTable capture_leaf_remap;
    if (!BuildRemappedHistoricalBaseScratch(target, saved_base.id(), scratch,
                                            historical_layers, &live_references,
                                            &capture_leaf_remap,
                                            &owner_live_refs)) {
      obj_id = owner_id;
      base = saved_base;
      journal = std::move(saved_journal);
      assert(false && "BuildRemappedHistoricalBaseScratch failed");
    }
    for (auto const& [layer_id, layer_map] : historical_layers.state) {
      if (!layer_id.is_valid() || !layer_map.has_value()) {
        continue;
      }
      if (domain->Find(layer_id)) {
        live_references.insert(layer_id);
      }
    }
    ae::Ptr<ae::Obj> const base_live = domain->Find(saved_base.id());
    assert(base_live);
    std::map<ae::ObjId, ae::ObjId> loaded_to_live_id_remap;
    for (auto const& [old_key, new_key] : capture_leaf_remap) {
      if (old_key == new_key || old_key == saved_base.id().id()) {
        continue;
      }
      ae::ObjId const live_id{old_key};
      ae::ObjId const scratch_id{new_key};
      if (!live_id.is_valid() || !scratch_id.is_valid() ||
          live_id == scratch_id) {
        continue;
      }
      if (!owner_live_refs.count(live_id) && !live_references.count(live_id)) {
        continue;
      }
      if (domain->Find(live_id)) {
        loaded_to_live_id_remap[scratch_id] = live_id;
      }
    }
    bool const rebind_historical_refs = !loaded_to_live_id_remap.empty();
    if (!InjectAllLayeredObjects(historical_layers, domain->domain_storage())) {
      obj_id = owner_id;
      base = saved_base;
      journal = std::move(saved_journal);
      assert(false && "InjectAllLayeredObjects for historical base failed");
    }
    {
      ae::DomainGraph load_graph{domain};
      auto const load_result = load_graph.Load(
          static_cast<ConcreteNode&>(*base_live), saved_base.id());
      if (!load_result.has_value() || !load_result->IsOk()) {
        obj_id = owner_id;
        base = saved_base;
        journal = std::move(saved_journal);
        assert(false && "DomainGraph::Load for historical base failed");
      }
    }
    if (rebind_historical_refs) {
      ae::Ptr<ae::Obj> const root = base_live;
      std::map<ae::ObjId, ae::Ptr<ae::Obj>> extra_live;
      for (ae::ObjId const id : live_references) {
        if (ae::Ptr<ae::Obj> const obj = domain->Find(id)) {
          extra_live[id] = obj;
        }
      }
      for (auto const& [_, live_id] : loaded_to_live_id_remap) {
        if (ae::Ptr<ae::Obj> const obj = domain->Find(live_id)) {
          extra_live[live_id] = obj;
        }
      }
      if (!RemapLiveObjectGraph(*domain, root, {}, {}, extra_live,
                                &loaded_to_live_id_remap)) {
        obj_id = owner_id;
        base = saved_base;
        journal = std::move(saved_journal);
        assert(false && "RemapLiveObjectGraph for historical base failed");
      }
    }

    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
  }

  // Longest collapsible prefix: records that neither count nor age retention
  // needs. Empty when compaction is blocked or retention is unlimited.
  std::size_t CollapsiblePrefixCount(std::uint64_t now_us) const {
    if (journal_compaction_blocked_ || journal.empty()) {
      return 0;
    }
    auto const& policy = journal_retention_policy_;
    for (std::size_t i = 0; i < journal.size(); ++i) {
      bool retain = false;
      if (policy.max_events == JournalRetentionPolicy::kUnlimitedEvents) {
        retain = true;
      } else if (journal.size() - i <= policy.max_events) {
        retain = true;
      }
      if (policy.max_age.has_value()) {
        if (journal[i].retained_since_us > now_us) {
          retain = true;
        } else {
          auto const age_us = now_us - journal[i].retained_since_us;
          if (age_us <= static_cast<std::uint64_t>(policy.max_age->count())) {
            retain = true;
          }
        }
      }
      if (retain) {
        return i;
      }
      auto const& event = journal[i].event;
      if (!event.is_valid() || !event.is_loaded()) {
        std::abort();
      }
      if (!event->CanCompactStoredState()) {
        return i;
      }
    }
    return journal.size();
  }

  template <typename ConcreteNode>
  void CompactJournalInto(ConcreteNode& target, std::uint64_t now_us) {
    auto const collapse = CollapsiblePrefixCount(now_us);
    if (collapse == 0) {
      return;
    }

    assert(domain != nullptr);
    ae::IDomainStorage& storage = domain->domain_storage();
    if (!detail::CanCompactStoredObjectVersions<ConcreteNode>(storage,
                                                              target.obj_id)) {
      return;
    }
    if (target.base.is_valid() && target.base.is_loaded()) {
      if (!detail::CanCompactStoredObjectVersions<ConcreteNode>(
              storage, target.base.id())) {
        return;
      }
    }

    auto const saved_generation = generation_;
    std::vector<EventRecord> retained(
        journal.begin() + static_cast<std::ptrdiff_t>(collapse),
        journal.end());

    suppress_materialized_change_ = true;
    typename ConcreteNode::CompactionOperationScratch compaction_scratch{};
    target.FoldCollapsedJournalPrefixIntoReachableStorage(collapse,
                                                         &compaction_scratch);
    target.StashLiveReachableLayersBeforeHistoricalCapture(
        &compaction_scratch);

    if (retained.empty()) {
      journal.clear();
      CaptureBaseStateInto(target);
      target.ReleaseFoldCaptureMaterialization(&compaction_scratch);
      target.RestoreLiveReachableLayersAfterHistoricalCapture(
          &compaction_scratch);
      generation_ = saved_generation;
      applied_journal_size_ = kJournalFullyMaterialized;
      ae::DomainGraph graph{domain,
                            ae::GraphSerializationScope::LocalPersistent};
      graph.force_materialize = true;
      ae::ObjId const owner_id = obj_id;
      std::optional<ae::seri::SeriResult> const save_result =
          graph.Save(static_cast<Node const&>(target), owner_id);
      if (!save_result || !save_result->IsOk()) {
        std::abort();
      }
      suppress_materialized_change_ = false;
      return;
    }

    journal.erase(journal.begin() + static_cast<std::ptrdiff_t>(collapse),
                  journal.end());

    RebuildFromBaseAndReplay(target);
    journal.clear();
    CaptureBaseStateInto(target);
    target.ReleaseFoldCaptureMaterialization(&compaction_scratch);
    target.RestoreLiveReachableLayersAfterHistoricalCapture(
        &compaction_scratch);
    journal = std::move(retained);
    ReplayJournal();
    suppress_materialized_change_ = false;
    generation_ = saved_generation;
    applied_journal_size_ = kJournalFullyMaterialized;
  }

  // Speculative insertion is for disposable scratch copies only.
  // If historical replay fails, the scratch object is left partially modified
  // and must be discarded.
  template <typename ConcreteNode>
  bool TryInsertEvent(ConcreteNode& target, EventRecord record) {
    if (domain == nullptr || !base.is_valid() || !base.is_loaded() ||
        !record.event.is_valid() || !record.event.is_loaded() ||
        record.event.domain() != domain || record.order.timestamp_us == 0) {
      return false;
    }

    if (record.retained_since_us == 0) {
      record.retained_since_us = SystemUtcMicros();
    }

    // A record with a timestamp already present lands before the records that
    // carry it. That is what lower_bound does on this replica, not an agreed
    // rule: where equal timestamps end up relative to each other is still an
    // open question, and another replica may place them the other way round.
    auto position = std::lower_bound(journal.begin(), journal.end(), record,
                                     EventRecordOrderLess);

    bool const appended = position == journal.end();
    journal.insert(position, std::move(record));

    if (appended) {
      if (applied_journal_size_ == kJournalFullyMaterialized) {
        applied_journal_size_ = journal.size() - 1;
      }
      if (!TryEnsureCurrentGeneration()) {
        journal.pop_back();
        applied_journal_size_ = journal.size();
        return false;
      }
      return true;
    } else {
      return TryRebuildFromBaseAndReplay(target);
    }
  }

  template <typename ConcreteNode>
  void InsertEvent(ConcreteNode& target, EventRecord record) {
    bool const ok = TryInsertEvent(target, std::move(record));
    assert(ok && "InsertEvent invariant broken");
    (void)ok;
  }

  // Non-shared local commit: empty identity, current time as order.
  template <typename ConcreteNode>
  void CommitInto(ConcreteNode& target, Event::ptr event) {
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event->CanApplyTo(target));

    // Timestamp adjustment, not a logical clock: a Node's own consecutive
    // commits keep the order they were made in even when the wall clock does
    // not advance between them.
    std::uint64_t timestamp_us = SystemUtcMicros();
    if (!journal.empty()) {
      auto const& last = journal.back().order;
      if (timestamp_us <= last.timestamp_us) {
        timestamp_us = last.timestamp_us + 1;
      }
    }

    EventRecord record{
        .event = std::move(event),
        .order = EventOrder{.timestamp_us = timestamp_us},
        .retained_since_us = SystemUtcMicros(),
    };
    InsertEvent(target, std::move(record));
  }

 private:
  virtual void CaptureBaseStateImpl() {
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual void CommitImpl(Event::ptr event) {
    (void)event;
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual void CompactJournalImpl(std::uint64_t now_us) {
    (void)now_us;
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual bool TryReplayFromBaseImpl() {
    assert(false && "Concrete Node must inherit through NodeFor");
    return false;
  }

  static constexpr std::size_t kJournalFullyMaterialized =
      (std::numeric_limits<std::size_t>::max)();

  void* materialized_change_ctx_;
  MaterializedChangeFn materialized_change_fn_;

  std::uint64_t generation_;
  std::size_t applied_journal_size_;
  JournalRetentionPolicy journal_retention_policy_;
  bool journal_compaction_blocked_;
  bool suppress_materialized_change_;
};

// Ordered unique dirty set for GUI publication. Entries are ObjIds, not Node
// pointers: while the GUI holds an unread publication a pending Node may be
// removed from live topology by a later Event, and the graph is then free to
// drop it. The publication step resolves each id against live reachability
// and skips ids that no longer resolve. First-dirty order is preserved;
// membership prevents duplicate entries while Events keep coalescing into one
// pending entry until published.
void TrackMaterializedChangeNotifierCopy(Node& target);
void PendingDirtyNodesNotify(void* ctx, Node& node);

struct PendingDirtyNodes {
  void Subscribe(Node& node) {
    notifier_subscribed_.insert(node.obj_id.id());
    node.BindMaterializedChangeNotifier(this, &PendingDirtyNodesNotify);
  }

  void OnNotifierCopiedTo(Node& node) {
    if (node.MaterializedChangeCallback() == &PendingDirtyNodesNotify &&
        node.MaterializedChangeContext() == this) {
      notifier_subscribed_.insert(node.obj_id.id());
    }
  }

  void ClearAllSubscribedNotifiers(ae::Domain& domain) {
    for (std::uint32_t const id : notifier_subscribed_) {
      if (ae::Ptr<ae::Obj> obj = domain.Find(ae::ObjId{id})) {
        Node& node = static_cast<Node&>(*obj);
        if (node.MaterializedChangeContext() == this) {
          node.ClearMaterializedChangeNotifier();
        }
      }
    }
    notifier_subscribed_.clear();
    Clear();
  }

  void Note(Node& node) {
    auto const id = node.obj_id.id();
    if (membership.insert(id).second) {
      ordered.push_back(id);
    }
  }

  [[nodiscard]] bool empty() const { return ordered.empty(); }

  void Clear() {
    ordered.clear();
    membership.clear();
  }

  std::uint32_t PopFront() {
    assert(!ordered.empty());
    std::uint32_t const id = ordered.front();
    ordered.erase(ordered.begin());
    membership.erase(id);
    return id;
  }

  std::vector<std::uint32_t> ordered;
  std::unordered_set<std::uint32_t> membership;
  std::unordered_set<std::uint32_t> notifier_subscribed_;
};

inline void PendingDirtyNodesNotify(void* ctx, Node& node) {
  static_cast<PendingDirtyNodes*>(ctx)->Note(node);
}

inline void Node::CopyMaterializedChangeNotifierFrom(Node const& source) {
  materialized_change_ctx_ = source.materialized_change_ctx_;
  materialized_change_fn_ = source.materialized_change_fn_;
  TrackMaterializedChangeNotifierCopy(*this);
}

}  // namespace apptraverse

#include "apptraverse/object_clone_historical.inl"

#endif  // APPTRAVERSE_NODE_H_
