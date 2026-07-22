#ifndef TESTS_JOURNAL_SYNC_FIXTURE_H_
#define TESTS_JOURNAL_SYNC_FIXTURE_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/event_record.h"
#include "apptraverse/node_macros.h"
#include "apptraverse/replica_id.h"

namespace apptraverse::test {

inline constexpr ae::ObjId::Type kSyncNodeId = 1;
inline constexpr ae::ObjId::Type kSyncFactoryId = 2;
inline constexpr ae::ObjId::Type kSyncEventPrototypeId = 3;
inline constexpr ae::ObjId::Type kSyncBaseSnapshotId = 200;
inline constexpr ReplicaId kReplicaA{1};
inline constexpr ReplicaId kReplicaB{2};

#define APPTRAVERSE_CHECK(cond)                                           \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'     \
                << __LINE__ << ")\n";                                     \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

class SyncNode;
class SyncFactory;
class AppendTokenEvent;

class SyncNode : public apptraverse::Node {
  AT_NODE_OBJECT(SyncNode, apptraverse::Node, 0)

  SyncNode() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  SyncNode(ae::ObjProp prop, ae::ObjId factory_id)
      : Node{prop}, factory_id_{factory_id} {}
#endif

 public:
  void InitializeReplicaForTest(ReplicaId replica_id) {
    InitializeReplica(replica_id);
  }

  bool AppendToken(ae::ObjId snapshot_id, ae::ObjId event_id,
                   std::int32_t token);

  ae::ObjId factory_id() const { return factory_id_; }
  std::vector<std::int32_t> const& tokens() const { return tokens_; }
  ae::ObjId base_snapshot_id_for_test() const { return BaseSnapshotId(); }
  std::size_t journal_size_for_test() const { return JournalSize(); }
  ReplicaId replica_id_for_test() const { return replica_id(); }
  std::uint64_t next_local_sequence_for_test() const {
    return next_local_sequence();
  }
  std::uint64_t logical_clock_for_test() const { return logical_clock(); }
  EventIdentity journal_identity_at_for_test(std::size_t index) const {
    return JournalIdentityAt(index);
  }
  std::uint64_t journal_logical_time_at_for_test(std::size_t index) const {
    return JournalLogicalTimeAt(index);
  }
  EventDeliveryState journal_delivery_state_at_for_test(
      std::size_t index) const {
    return JournalDeliveryStateAt(index);
  }
  std::uint32_t apply_calls_for_test() const { return apply_calls_; }

 private:
  friend class apptraverse::EventFor<SyncNode, AppendTokenEvent>;

  void Apply(AppendTokenEvent const& event);
  ae::ObjPtr<SyncFactory> ResolveFactory();

  ae::ObjId factory_id_;
  std::vector<std::int32_t> tokens_{};
  std::uint32_t apply_calls_{0};

  AT_NODE_STATE(factory_id_, tokens_)
};

class AppendTokenEvent
    : public apptraverse::EventFor<SyncNode, AppendTokenEvent> {
  AE_OBJECT(AppendTokenEvent, apptraverse::Event, 0)

  AppendTokenEvent() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  AppendTokenEvent(ae::ObjProp prop, std::int32_t token)
      : EventFor{prop}, token_{token} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(token_))

  std::int32_t token() const { return token_; }

 private:
  friend class SyncFactory;

  void Initialize(SyncFactory const&, std::int32_t token) { token_ = token; }

  std::int32_t token_{0};
};

class SyncFactory : public ae::Obj {
  AE_OBJECT(SyncFactory, ae::Obj, 0)

  SyncFactory() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  SyncFactory(ae::ObjProp prop, AppendTokenEvent::ptr event_prototype)
      : ae::Obj{prop}, event_prototype_{std::move(event_prototype)} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(event_prototype_))

 private:
  friend class SyncNode;

  AppendTokenEvent::ptr CreateAppendTokenEvent(ae::ObjId event_id,
                                               std::int32_t token);

  AppendTokenEvent::ptr event_prototype_;
};

inline void SyncNode::Apply(AppendTokenEvent const& event) {
  tokens_.push_back(event.token());
  ++apply_calls_;
}

inline ae::ObjPtr<SyncFactory> SyncNode::ResolveFactory() {
  if (domain == nullptr || !factory_id_.IsValid()) {
    return {};
  }
  auto factory = SyncFactory::ptr::Declare(
      ae::CreateWith{*domain}.with_id(factory_id_));
  factory.Load();
  if (!factory) {
    return {};
  }
  return factory;
}

inline bool SyncNode::AppendToken(ae::ObjId snapshot_id, ae::ObjId event_id,
                                  std::int32_t token) {
  auto factory = ResolveFactory();
  if (!factory) {
    return false;
  }
  auto event = factory->CreateAppendTokenEvent(event_id, token);
  if (!event) {
    return false;
  }
  CommitEvent(event, snapshot_id);
  return true;
}

inline AppendTokenEvent::ptr SyncFactory::CreateAppendTokenEvent(
    ae::ObjId event_id, std::int32_t token) {
  if (!event_id.IsValid() || !event_prototype_.is_valid()) {
    return {};
  }
  event_prototype_.Load();
  if (!event_prototype_) {
    return {};
  }
  auto clone = event_prototype_.Clone(event_id);
  if (!clone) {
    return {};
  }
  clone->Initialize(*this, token);
  return clone;
}

}  // namespace apptraverse::test

#endif  // TESTS_JOURNAL_SYNC_FIXTURE_H_
