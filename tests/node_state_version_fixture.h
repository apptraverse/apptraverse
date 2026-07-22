#ifndef TESTS_NODE_STATE_VERSION_FIXTURE_H_
#define TESTS_NODE_STATE_VERSION_FIXTURE_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_macros.h"
#include "apptraverse/replica_id.h"

namespace apptraverse::test {

inline constexpr ae::ObjId::Type kVersionedNode3Id = 1;
inline constexpr ae::ObjId::Type kVersionedFactoryId = 2;
inline constexpr ae::ObjId::Type kSetBaseValueEventPrototypeId = 3;
inline constexpr ae::ObjId::Type kSetBaseValueEventId = 100;
inline constexpr ae::ObjId::Type kBaseSnapshotId = 200;
inline constexpr ae::ObjId::Type kUnusedSnapshotId = 201;

#if defined(APPTRAVERSE_STATE_SCHEMA_V0)
inline constexpr std::uint32_t kStateSchemaVersion = 0;
#else
inline constexpr std::uint32_t kStateSchemaVersion = 1;
#endif

#define APPTRAVERSE_CHECK(cond)                                          \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'    \
                << __LINE__ << ")\n";                                    \
      std::exit(1);                                                      \
    }                                                                    \
  } while (0)

class VersionedNode2;
class VersionedNode3;
class VersionedFactory;
class SetBaseValueEvent;

class VersionedNode2 : public apptraverse::Node {
  AT_NODE_OBJECT(VersionedNode2, apptraverse::Node, kStateSchemaVersion)

 protected:
  VersionedNode2() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD) && \
    defined(APPTRAVERSE_STATE_SCHEMA_V0)
 public:
  VersionedNode2(ae::ObjProp prop, ae::ObjId factory_id,
                 std::int32_t legacy_base_value)
      : Node{prop},
        factory_id_{factory_id},
        legacy_base_value_{legacy_base_value} {}
#endif

 public:
  bool SetBaseValue(ae::ObjId snapshot_id, ae::ObjId event_id,
                    std::int64_t value);

  ae::ObjId factory_id() const { return factory_id_; }

  ae::ObjId base_snapshot_id_for_test() const { return BaseSnapshotId(); }
  std::size_t journal_size_for_test() const { return JournalSize(); }
  std::uint32_t set_base_apply_calls() const { return set_base_apply_calls_; }

  void InitializeReplicaForTest(apptraverse::ReplicaId replica_id) {
    InitializeReplica(replica_id);
  }

#if defined(APPTRAVERSE_STATE_SCHEMA_V0)
  std::int32_t legacy_base_value_for_test() const {
    return legacy_base_value_;
  }

  std::int64_t logical_base_value_for_test() const {
    return static_cast<std::int64_t>(legacy_base_value_) * 10;
  }

  void CorruptBaseForTest(std::int32_t legacy_base_value) {
    legacy_base_value_ = legacy_base_value;
  }
#else
  std::int64_t base_value_for_test() const { return base_value_; }
  bool enabled_for_test() const { return enabled_; }

  std::int64_t logical_base_value_for_test() const { return base_value_; }

  void CorruptBaseForTest(std::int64_t base_value, bool enabled) {
    base_value_ = base_value;
    enabled_ = enabled;
  }
#endif

 private:
  friend class apptraverse::EventFor<VersionedNode2, SetBaseValueEvent>;

  void Apply(SetBaseValueEvent const& event);
  ae::ObjPtr<VersionedFactory> ResolveFactory();

  ae::ObjId factory_id_;
  std::uint32_t set_base_apply_calls_{0};

#if defined(APPTRAVERSE_STATE_SCHEMA_V0)
  std::int32_t legacy_base_value_{7};

  AT_NODE_STATE(factory_id_, legacy_base_value_)
#else
  std::int64_t base_value_{0};
  bool enabled_{false};

 public:
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      std::int32_t legacy_base_value = 0;
      dnv(factory_id_, legacy_base_value);
      base_value_ = static_cast<std::int64_t>(legacy_base_value) * 10;
      enabled_ = true;
    }
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      auto legacy_base_value = static_cast<std::int32_t>(base_value_ / 10);
      dnv(factory_id_, legacy_base_value);
    }
  }

 private:
  AT_NODE_STATE(factory_id_, base_value_, enabled_)
#endif
};

class VersionedNode3 : public VersionedNode2 {
  AT_NODE_OBJECT(VersionedNode3, VersionedNode2, kStateSchemaVersion)

  VersionedNode3() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD) && \
    defined(APPTRAVERSE_STATE_SCHEMA_V0)
 public:
  VersionedNode3(ae::ObjProp prop, ae::ObjId factory_id,
                 std::int32_t legacy_base_value,
                 std::int32_t legacy_derived_value)
      : VersionedNode2{prop, factory_id, legacy_base_value},
        legacy_derived_value_{legacy_derived_value} {}
#endif

 public:
#if defined(APPTRAVERSE_STATE_SCHEMA_V0)
  std::int32_t legacy_derived_value_for_test() const {
    return legacy_derived_value_;
  }

  std::int64_t logical_derived_value_for_test() const {
    return static_cast<std::int64_t>(legacy_derived_value_) * 100;
  }

  void CorruptDerivedForTest(std::int32_t legacy_derived_value) {
    legacy_derived_value_ = legacy_derived_value;
  }
#else
  std::int64_t derived_value_for_test() const { return derived_value_; }
  std::uint32_t generation_for_test() const { return generation_; }

  std::int64_t logical_derived_value_for_test() const {
    return derived_value_;
  }

  void CorruptDerivedForTest(std::int64_t derived_value,
                             std::uint32_t generation) {
    derived_value_ = derived_value;
    generation_ = generation;
  }
#endif

 private:
#if defined(APPTRAVERSE_STATE_SCHEMA_V0)
  std::int32_t legacy_derived_value_{3};

  AT_NODE_STATE(legacy_derived_value_)
#else
  std::int64_t derived_value_{0};
  std::uint32_t generation_{0};

 public:
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      std::int32_t legacy_derived_value = 0;
      dnv(legacy_derived_value);
      derived_value_ =
          static_cast<std::int64_t>(legacy_derived_value) * 100;
      generation_ = 1;
    }
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      auto legacy_derived_value =
          static_cast<std::int32_t>(derived_value_ / 100);
      dnv(legacy_derived_value);
    }
  }

 private:
  AT_NODE_STATE(derived_value_, generation_)
#endif
};

class SetBaseValueEvent
    : public apptraverse::EventFor<VersionedNode2, SetBaseValueEvent> {
  AE_OBJECT(SetBaseValueEvent, apptraverse::Event, 0)

  SetBaseValueEvent() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  SetBaseValueEvent(ae::ObjProp prop, std::int64_t value)
      : EventFor{prop}, value_{value} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(value_))

  std::int64_t value() const { return value_; }

 private:
  friend class VersionedFactory;

  void Initialize(VersionedFactory const&, std::int64_t value) {
    value_ = value;
  }

  std::int64_t value_{0};
};

class VersionedFactory : public ae::Obj {
  AE_OBJECT(VersionedFactory, ae::Obj, 0)

  VersionedFactory() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  VersionedFactory(ae::ObjProp prop,
                   SetBaseValueEvent::ptr set_base_value_event_prototype)
      : ae::Obj{prop},
        set_base_value_event_prototype_{
            std::move(set_base_value_event_prototype)} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(set_base_value_event_prototype_))

 private:
  friend class VersionedNode2;

  SetBaseValueEvent::ptr CreateSetBaseValueEvent(ae::ObjId event_id,
                                                 std::int64_t value);

  SetBaseValueEvent::ptr set_base_value_event_prototype_;
};

inline void VersionedNode2::Apply(SetBaseValueEvent const& event) {
  ++set_base_apply_calls_;
#if defined(APPTRAVERSE_STATE_SCHEMA_V0)
  legacy_base_value_ = static_cast<std::int32_t>(event.value() / 10);
#else
  base_value_ = event.value();
  enabled_ = true;
#endif
}

inline ae::ObjPtr<VersionedFactory> VersionedNode2::ResolveFactory() {
  if (domain == nullptr || !factory_id_.IsValid()) {
    return {};
  }

  auto factory = VersionedFactory::ptr::Declare(
      ae::CreateWith{*domain}.with_id(factory_id_));
  factory.Load();
  if (!factory) {
    return {};
  }
  return factory;
}

inline bool VersionedNode2::SetBaseValue(ae::ObjId snapshot_id,
                                         ae::ObjId event_id,
                                         std::int64_t value) {
  auto factory = ResolveFactory();
  if (!factory) {
    return false;
  }

  auto event = factory->CreateSetBaseValueEvent(event_id, value);
  if (!event) {
    return false;
  }

  CommitEvent(event, snapshot_id);
  return true;
}

inline SetBaseValueEvent::ptr VersionedFactory::CreateSetBaseValueEvent(
    ae::ObjId event_id, std::int64_t value) {
  if (!event_id.IsValid() || !set_base_value_event_prototype_.is_valid()) {
    return {};
  }

  set_base_value_event_prototype_.Load();
  if (!set_base_value_event_prototype_) {
    return {};
  }

  auto clone = set_base_value_event_prototype_.Clone(event_id);
  if (!clone) {
    return {};
  }

  clone->Initialize(*this, value);
  return clone;
}

}  // namespace apptraverse::test

#endif  // TESTS_NODE_STATE_VERSION_FIXTURE_H_
