#ifndef APPTRAVERSE_OBJECT_LINK_H_
#define APPTRAVERSE_OBJECT_LINK_H_

#include <type_traits>
#include <utility>

#include "aether/obj/obj_ptr.h"

namespace apptraverse {

enum class LinkScope {
  kShared,
  kLocal,
};

// Thin compile-time-scoped wrapper around ae::ObjPtr<T>.
// Scope is schema metadata on the field type and is never serialized.
// Wire/storage representation is identical to ae::ObjPtr (ObjId + ObjFlags).
template <typename T, LinkScope Scope>
class ObjectLink {
  template <typename U, LinkScope S>
  friend class ObjectLink;

  template <typename U, LinkScope S>
  friend ae::imstream<ae::DomainBufferReader>& operator>>(
      ae::imstream<ae::DomainBufferReader>& is, ObjectLink<U, S>& link);

  template <typename U, LinkScope S>
  friend ae::omstream<ae::DomainBufferWriter>& operator<<(
      ae::omstream<ae::DomainBufferWriter>& os, ObjectLink<U, S> const& link);

 public:
  using element_type = T;
  static constexpr LinkScope kScope = Scope;

  ObjectLink() = default;

  ObjectLink(ae::ObjPtr<T> ptr) noexcept : ptr_{std::move(ptr)} {}

  template <typename U>
    requires(ae::IsAbleToCast<T, U>::value)
  ObjectLink(ae::ObjPtr<U> ptr) noexcept : ptr_{std::move(ptr)} {}

  template <typename U, LinkScope OtherScope>
    requires(ae::IsAbleToCast<T, U>::value)
  ObjectLink(ObjectLink<U, OtherScope> const& other) noexcept
      : ptr_{other.ptr_} {}

  template <typename U, LinkScope OtherScope>
    requires(ae::IsAbleToCast<T, U>::value)
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
    requires(ae::IsAbleToCast<T, U>::value)
  ObjectLink& operator=(ae::ObjPtr<U> ptr) noexcept {
    ptr_ = std::move(ptr);
    return *this;
  }

  template <typename U, LinkScope OtherScope>
    requires(ae::IsAbleToCast<T, U>::value)
  ObjectLink& operator=(ObjectLink<U, OtherScope> const& other) noexcept {
    ptr_ = other.ptr_;
    return *this;
  }

  template <typename U, LinkScope OtherScope>
    requires(ae::IsAbleToCast<T, U>::value)
  ObjectLink& operator=(ObjectLink<U, OtherScope>&& other) noexcept {
    ptr_ = std::move(other.ptr_);
    return *this;
  }

  ae::ObjId id() const { return ptr_.id(); }
  ae::ObjFlags flags() const { return ptr_.flags(); }
  ae::Domain* domain() const { return ptr_.domain(); }
  void SetFlags(ae::ObjFlags flags) { ptr_.SetFlags(flags); }

  bool is_valid() const { return ptr_.is_valid(); }
  bool is_loaded() const { return ptr_.is_loaded(); }
  explicit operator bool() const { return static_cast<bool>(ptr_); }

  ae::Ptr<T> const& Load() { return ptr_.Load(); }
  ae::Ptr<T> const& Load() const { return ptr_.Load(); }
  void Save() const { ptr_.Save(); }
  void Reset() { ptr_.Reset(); }

  ae::Ptr<T> const& operator->() { return ptr_.operator->(); }
  ae::Ptr<T> const& operator->() const { return ptr_.operator->(); }
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

template <typename T, LinkScope Scope>
ae::imstream<ae::DomainBufferReader>& operator>>(
    ae::imstream<ae::DomainBufferReader>& is, ObjectLink<T, Scope>& link) {
  is >> link.ptr_;
  return is;
}

template <typename T, LinkScope Scope>
ae::omstream<ae::DomainBufferWriter>& operator<<(
    ae::omstream<ae::DomainBufferWriter>& os, ObjectLink<T, Scope> const& link) {
  os << link.ptr_;
  return os;
}

}  // namespace apptraverse

namespace ae::reflect {

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

// Shared edges are separate Nodes: discovery enqueues them via OverrideFunc
// and must not traverse them inline as owned state.
template <typename T>
struct NodeVisitor<apptraverse::SharedPtr<T>> {
  using Policy = AnyPolicyMatch;

  template <typename Visitor>
  void Visit(apptraverse::SharedPtr<T>&, CycleDetector&, Visitor&&) const {}

  template <typename Visitor>
  void Visit(apptraverse::SharedPtr<T> const&, CycleDetector&,
             Visitor&&) const {}
};

}  // namespace ae::reflect

#endif  // APPTRAVERSE_OBJECT_LINK_H_
