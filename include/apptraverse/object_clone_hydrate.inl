#ifndef APPTRAVERSE_OBJECT_CLONE_HYDRATE_INL_
#define APPTRAVERSE_OBJECT_CLONE_HYDRATE_INL_

#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse {

bool InjectAllLayeredObjects(ae::RamDomainStorage const& scratch,
                             ae::IDomainStorage& storage);

// Read operation-local bytes with the destination domain as the identity and
// pointer-lifetime owner. Newly materialized objects are retained by graph edges,
// including references whose on-disk flags request lazy loading.
bool HydrateLiveObjectFromLayerScratch(
    ae::Domain& target, ae::Obj& object, ae::ObjId storage_id,
    ae::RamDomainStorage const& layered_scratch,
    std::set<ae::ObjId> const& live_reference_ids);

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_CLONE_HYDRATE_INL_
