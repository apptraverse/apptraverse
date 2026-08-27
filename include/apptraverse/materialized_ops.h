#ifndef APPTRAVERSE_MATERIALIZED_OPS_H_
#define APPTRAVERSE_MATERIALIZED_OPS_H_

#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aether-miscpp/types/result.h"
#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/details/tags.h"
#include "aether/obj/domain.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event_record.h"
#include "apptraverse/graph_walk.h"
#include "apptraverse/node.h"
#include "apptraverse/publication_channel.h"

namespace apptraverse {

struct MaterializedOps {
  void (*collect_ptrs)(ae::Obj& obj, std::vector<ae::Obj*>& stack);
  void (*serialize)(ae::Obj const& obj, ByteSink& out);
  void (*deserialize)(ae::Obj& obj, ByteSource& in, ae::Domain& ui_domain);
};

inline std::unordered_map<std::uint32_t, MaterializedOps>&
MaterializedOpsMap() {
  static std::unordered_map<std::uint32_t, MaterializedOps> map;
  return map;
}

inline void RegisterMaterializedOps(std::uint32_t class_id,
                                    MaterializedOps ops) {
  MaterializedOpsMap()[class_id] = ops;
}

inline MaterializedOps const* FindMaterializedOps(std::uint32_t class_id) {
  auto it = MaterializedOpsMap().find(class_id);
  if (it == MaterializedOpsMap().end()) {
    return nullptr;
  }
  return &it->second;
}

namespace detail {

using UiBin = ae::seri::BinaryArchive<ae::seri::BinaryVectorBuffer<>>;

template <typename SizeType = std::uint32_t>
class BinaryConstSpanBuffer {
 public:
  BinaryConstSpanBuffer(std::uint8_t const* data, std::size_t size) noexcept
      : data_{data}, size_{size} {}

  ae::seri::SeriResult Write(ae::seri::SizeWriteTag) {
    return ae::Error<ae::seri::SeriError>{ae::seri::write_eof};
  }
  ae::seri::SeriResult Write(ae::seri::DataWriteTag) {
    return ae::Error<ae::seri::SeriError>{ae::seri::write_eof};
  }

  ae::seri::SeriResult Read(ae::seri::SizeReadTag tag) {
    SizeType size{};
    auto const data_result =
        Read(ae::seri::DataReadTag{&size, sizeof(size)});
    if (!data_result) {
      return data_result;
    }
    tag.size = static_cast<std::size_t>(size);
    return ae::Ok{ae::seri::good};
  }

  ae::seri::SeriResult Read(ae::seri::DataReadTag tag) {
    if (read_position_ + tag.size > size_) {
      return ae::Error<ae::seri::SeriError>{ae::seri::read_eof};
    }
    std::memcpy(tag.data, data_ + read_position_, tag.size);
    read_position_ += tag.size;
    return ae::Ok{ae::seri::good};
  }

  std::size_t read_position() const noexcept { return read_position_; }

 private:
  std::uint8_t const* data_;
  std::size_t size_;
  std::size_t read_position_{0};
};

using UiReadBin = ae::seri::BinaryArchive<BinaryConstSpanBuffer<>>;

template <typename T>
constexpr bool IsObjPtr = false;

template <typename T>
struct ObjPtrTraits;

template <typename T>
struct ObjPtrTraits<ae::ObjPtr<T>> {
  using Target = T;
};

template <typename T>
constexpr bool IsObjPtr<ae::ObjPtr<T>> = true;

template <typename Visitor, typename T>
void VisitConcrete(T&& object, Visitor& visitor) {
  ae::domain_visitor::DomainVisit(
      std::forward<T>(object), visitor,
      ae::domain_visitor::PolicyConst<
          ae::domain_visitor::VisitPolicy::kShallow>{});
}

struct SaveMaterializedField {
  UiBin* archive;

  void operator()(auto const& value) {
    using U = std::remove_cvref_t<decltype(value)>;
    if constexpr (IsObjPtr<U>) {
      using Target = typename ObjPtrTraits<U>::Target;
      if constexpr (IsExecutionTarget<Target>) {
        return;
      }
      std::uint32_t const id = value.is_valid() ? value.id().id() : 0;
      archive->Save(id);
    } else if constexpr (std::is_same_v<U, std::vector<EventRecord>>) {
      return;
    } else if constexpr (std::is_pointer_v<U>) {
      if (value != nullptr) {
        VisitConcrete(*value, *this);
      }
    } else if constexpr (std::is_class_v<U> &&
                           std::is_base_of_v<ae::Obj, U>) {
      VisitConcrete(value, *this);
    } else {
      archive->Save(value);
    }
  }
};

template <typename Archive>
struct LoadMaterializedField {
  Archive* archive;
  ae::Domain* ui_domain;

  void operator()(auto& value) {
    using U = std::remove_cvref_t<decltype(value)>;
    if constexpr (IsObjPtr<U>) {
      using Target = typename ObjPtrTraits<U>::Target;
      if constexpr (IsExecutionTarget<Target>) {
        value = {};
        return;
      }
      std::uint32_t id = 0;
      archive->Load(id);
      if (id == 0) {
        value = {};
        return;
      }
      auto existing = ui_domain->Find(ae::ObjId{id});
      assert(existing);
      value = ae::ObjPtr<Target>::MakeFromThis(
          static_cast<Target*>(existing.get()));
    } else if constexpr (std::is_same_v<U, std::vector<EventRecord>>) {
      value.clear();
    } else if constexpr (std::is_pointer_v<U>) {
      if (value != nullptr) {
        VisitConcrete(*value, *this);
      }
    } else if constexpr (std::is_class_v<U> &&
                           std::is_base_of_v<ae::Obj, U>) {
      VisitConcrete(value, *this);
    } else {
      archive->Load(value);
    }
  }
};

}  // namespace detail

template <typename T>
struct MaterializedOpsRegistrar {
  MaterializedOpsRegistrar() {
    RegisterMaterializedOps(T::kClassId,
                            MaterializedOps{
                                &Collect,
                                &Serialize,
                                &Deserialize,
                            });
  }

  static void Collect(ae::Obj& obj, std::vector<ae::Obj*>& stack) {
    ForEachMaterializedPtrFieldOn(static_cast<T&>(obj), [&](auto& pointer) {
      pointer.Load();
      assert(pointer.is_loaded());
      stack.push_back(&*pointer);
    });
  }

  static void Serialize(ae::Obj const& obj, ByteSink& out) {
    auto archive = detail::UiBin{ae::seri::BinaryVectorBuffer<>{out.bytes}};
    detail::SaveMaterializedField visitor{&archive};
    detail::VisitConcrete(static_cast<T const&>(obj), visitor);
  }

  static void Deserialize(ae::Obj& obj, ByteSource& in, ae::Domain& ui_domain) {
    detail::BinaryConstSpanBuffer buffer{in.data + in.pos, in.size - in.pos};
    auto archive = detail::UiReadBin{buffer};
    detail::LoadMaterializedField<detail::UiReadBin> visitor{&archive, &ui_domain};
    detail::VisitConcrete(static_cast<T&>(obj), visitor);
    in.pos += buffer.read_position();
  }
};

inline void CollectReachableObjects(ae::Obj& root,
                                    std::vector<ae::Obj*>& out) {
  std::unordered_set<std::uint32_t> seen;
  std::vector<ae::Obj*> stack;
  stack.push_back(&root);
  while (!stack.empty()) {
    ae::Obj* obj = stack.back();
    stack.pop_back();
    if (!seen.insert(obj->obj_id.id()).second) {
      continue;
    }
    out.push_back(obj);
    auto const* ops = FindMaterializedOps(obj->GetClassId());
    assert(ops != nullptr);
    assert(ops->collect_ptrs != nullptr);
    ops->collect_ptrs(*obj, stack);
  }
}

inline void CollectReachableNodes(ae::Obj& root, std::vector<Node*>& out) {
  std::vector<ae::Obj*> objects;
  CollectReachableObjects(root, objects);
  for (ae::Obj* obj : objects) {
    if (auto* node = dynamic_cast<Node*>(obj)) {
      out.push_back(node);
    }
  }
}

inline void EagerLoadReachable(ae::Obj& root) {
  std::vector<ae::Obj*> objects;
  CollectReachableObjects(root, objects);
  (void)objects;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_MATERIALIZED_OPS_H_
