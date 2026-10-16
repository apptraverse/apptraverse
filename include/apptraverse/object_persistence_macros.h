#ifndef APPTRAVERSE_OBJECT_PERSISTENCE_MACROS_H_
#define APPTRAVERSE_OBJECT_PERSISTENCE_MACROS_H_

// Legacy ObjPtr clear walkers for runtime-only targets were removed.
// Use standard Save/Load on reflected fields; stale refs after load are
// cleared by SanitizeLoadedPersistentObjPtrs.

#endif  // APPTRAVERSE_OBJECT_PERSISTENCE_MACROS_H_
