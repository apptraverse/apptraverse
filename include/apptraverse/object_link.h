#ifndef APPTRAVERSE_OBJECT_LINK_H_
#define APPTRAVERSE_OBJECT_LINK_H_

#include <type_traits>
#include <utility>

#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/serialization.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_ptr.h"

namespace apptraverse {

// Compile-time scope on a graph edge. Never written to storage/wire by itself;
// wire representation of ObjectLink is identical to ae::ObjPtr.
enum class LinkScope {
  kShared,
  kLocal,
};

// Thin scoped wrapper around ae::ObjPtr<T>.
//
// kShared / ordinary ObjPtr: included in local Save/Load and network shared
// graph serialization.
// kLocal: included in local Save/Load; cleared before network shared-graph
// serialization so referent objects are not exported.
template <typename T, LinkScope Scope>
class ObjectLink {
  template <typename U, LinkScope S>
  friend class ObjectLink;

 public:
  using element_type = T;
  static constexpr LinkScope kScope = Scope;

  ObjectLink() = default;

  ObjectLink(ae::ObjPtr<T> ptr) noexcept : ptr_{std::move(ptr)} {}

  template <typename U>
    requires(ae::AbleToCast<T, U>)
  ObjectLink(ae::ObjPtr<U> ptr) noexcept : ptr_{std::move(ptr)} {}

  template <typename U, LinkScope OtherScope>
    requires(ae::AbleToCast<T, U>)
  ObjectLink(ObjectLink<U, OtherScope> const& other) noexcept
      : ptr_{other.ptr_} {}

  template <typename U, LinkScope OtherScope>
    requires(ae::AbleToCast<T, U>)
  ObjectLink(ObjectLink<U, OtherScope>&& other) noexcept
      : ptr_{std::move(other.ptr_)} {}

  ObjectLink(ObjectLink const&) = default;
  ObjectLink(ObjectLink&&) noexcept = default;
  ObjectLink& operator=(ObjectLink const&) = default;
  ObjectLink& operator=(ObjectLink&&) noexcept = default;

  ObjectLink& operator=(ae::ObjPtr<T> ptr) noexcept {
    ptr_ = std::move(ptr);
    return *this;
  }

  template <typename U>
    requires(ae::AbleToCast<T, U>)
  ObjectLink& operator=(ae::ObjPtr<U> ptr) noexcept {
    ptr_ = std::move(ptr);
    return *this;
  }

  ae::ObjId id() const { return ptr_.id(); }
  ae::ObjFlags flags() const { return ptr_.flags(); }
  ae::Domain* domain() const { return ptr_.domain(); }
  void SetFlags(ae::ObjFlags flags) { ptr_.SetFlags(flags); }

  bool is_valid() const { return ptr_.is_valid(); }
  bool is_loaded() const { return ptr_.is_loaded(); }
  explicit operator bool() const { return static_cast<bool>(ptr_); }

  ae::Ptr<T> Load() { return ptr_.Load(); }
  ae::Ptr<T> Load() const { return ptr_.Load(); }
  void Save() const { ptr_.Save(); }
  void Reset() { ptr_.Reset(); }

  T* operator->() { return ptr_.operator->(); }
  T* operator->() const { return ptr_.operator->(); }
  T& operator*() { return *ptr_; }
  T const& operator*() const { return *ptr_; }

  ae::ObjPtr<T>& as_obj_ptr() { return ptr_; }
  ae::ObjPtr<T> const& as_obj_ptr() const { return ptr_; }

  operator ae::ObjPtr<T>&() { return ptr_; }
  operator ae::ObjPtr<T> const&() const { return ptr_; }

 private:
  ae::ObjPtr<T> ptr_;
};

template <typename T>
using SharedPtr = ObjectLink<T, LinkScope::kShared>;

template <typename T>
using LocalPtr = ObjectLink<T, LinkScope::kLocal>;

template <typename T>
struct IsSharedPtr : std::false_type {};

template <typename T>
struct IsSharedPtr<SharedPtr<T>> : std::true_type {};

template <typename T>
struct IsLocalPtr : std::false_type {};

template <typename T>
struct IsLocalPtr<LocalPtr<T>> : std::true_type {};

}  // namespace apptraverse

namespace ae::seri {
template <typename T, apptraverse::LinkScope Scope>
struct Serializer<BinaryArchive<DomainBuffer>,
                  apptraverse::ObjectLink<T, Scope>> {
  using Archive = BinaryArchive<DomainBuffer>;
  using Link = apptraverse::ObjectLink<T, Scope>;

  SeriResult Seri(Archive& archive, Meta<Link const> meta) const {
    return archive.Save(Meta{meta.value.as_obj_ptr()});
  }

  SeriResult Deseri(Archive& archive, Meta<Link> meta) const {
    return archive.Load(Meta{meta.value.as_obj_ptr()});
  }
};
}  // namespace ae::seri

namespace ae::domain_visitor {

// Local edges are never followed by deep reflection traversal.
template <typename T>
struct NodeVisitor<apptraverse::LocalPtr<T>> {
  using Policy = AnyPolicyMatch;

  template <typename Visitor>
  void Visit(apptraverse::LocalPtr<T>&, CycleDetector&, Visitor&&) const {}

  template <typename Visitor>
  void Visit(apptraverse::LocalPtr<T> const&, CycleDetector&,
             Visitor&&) const {}
};

// Shared scoped edges behave like ObjPtr for visitor policies that walk
// pointers; network discovery may treat them separately from LocalPtr.
template <typename T>
struct NodeVisitor<apptraverse::SharedPtr<T>> : NodeVisitor<ae::ObjPtr<T>> {};

}  // namespace ae::domain_visitor

#endif  // APPTRAVERSE_OBJECT_LINK_H_
