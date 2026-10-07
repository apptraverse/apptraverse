#ifndef APPTRAVERSE_OBJECT_PERSISTENCE_MACROS_H_
#define APPTRAVERSE_OBJECT_PERSISTENCE_MACROS_H_

#include "apptraverse/graph_walk.h"
#include "apptraverse/model_persistence.h"
#include "apptraverse/node.h"

// Walk reflected ObjPtr fields on the concrete DERIVED type during persistent
// snapshot save/load (Node& reflection only sees Node members).
#define APPTRAVERSE_REGISTER_PERSISTED_OBJPTR_WALKER(DERIVED)            \
  namespace {                                                            \
  void ClearPersistedObjPtrs_##DERIVED(                                  \
      ::apptraverse::Node& node,                                         \
      ::apptraverse::PersistentSnapshotPatchSession& session) {          \
    ::apptraverse::ForEachReflectedObjPtrOn(                             \
        static_cast<DERIVED&>(node), [&](auto& field) {                  \
          ::ae::ObjectPtrBase& ref = field;                              \
          if (!::apptraverse::ObjPtrPointsToExcludedTarget(ref)) {       \
            return;                                                      \
          }                                                              \
          session.RecordObjPtrPatch(ref);                                \
          field = std::decay_t<decltype(field)>{};                       \
        });                                                              \
  }                                                                      \
  void SanitizePersistedObjPtrs_##DERIVED(                               \
      ::apptraverse::Node& node,                                         \
      ::apptraverse::PersistentSnapshotPatchSession& session) {          \
    ::apptraverse::ForEachReflectedObjPtrOn(                             \
        static_cast<DERIVED&>(node), [&](auto& field) {                  \
          ::ae::ObjectPtrBase& ref = field;                              \
          if (!::apptraverse::ObjPtrReferenceIsStale(                    \
                  ref, session.loaded_object_ids())) {                   \
            return;                                                      \
          }                                                              \
          field = std::decay_t<decltype(field)>{};                       \
        });                                                              \
  }                                                                      \
  struct RegisterPersistedObjPtrWalker_##DERIVED {                       \
    RegisterPersistedObjPtrWalker_##DERIVED() {                          \
      ::apptraverse::RegisterPersistentObjPtrClearHook(                  \
          DERIVED::kClassId, ClearPersistedObjPtrs_##DERIVED);           \
      ::apptraverse::RegisterPersistentObjPtrSanitizeHook(                 \
          DERIVED::kClassId, SanitizePersistedObjPtrs_##DERIVED);        \
    }                                                                    \
  };                                                                     \
  RegisterPersistedObjPtrWalker_##DERIVED const                          \
      g_register_persisted_objptr_walker_##DERIVED{};                    \
  }  // namespace

#endif  // APPTRAVERSE_OBJECT_PERSISTENCE_MACROS_H_
