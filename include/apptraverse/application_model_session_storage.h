#ifndef APPTRAVERSE_APPLICATION_MODEL_SESSION_STORAGE_H_
#define APPTRAVERSE_APPLICATION_MODEL_SESSION_STORAGE_H_

#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse {

class ApplicationModelRuntime;

// Session-owned USER change buffer: runtime Save/Clone/history land in RAM;
// DirectoryDomainStorage is written only at explicit flush boundaries
// (unloaded subgraph close, application shutdown persist).
class ApplicationModelSessionStorage {
 public:
  ApplicationModelSessionStorage(DirectoryDomainStorage& disk_user,
                                 EmbeddedDistilledGraph const& graph,
                                 ae::IDomainStorage* disk_write_target = nullptr);

  OverlayDomainStorage& model_overlay() { return model_overlay_; }
  OverlayDomainStorage const& model_overlay() const { return model_overlay_; }
  DirectoryDomainStorage& disk_user() { return disk_user_; }
  ae::ObjId root_id() const { return root_id_; }

  void StagePersistentGraphInSessionOverlay(ae::Obj& root);
  void FlushReachableSubgraphToDisk(
      ae::Obj& subgraph_root, std::set<ae::ObjId> extra_flush_ids = {});
  void FlushEntireSessionOverlayToDisk();

  class ActivateScope {
   public:
    ActivateScope(ApplicationModelSessionStorage& storage,
                  ApplicationModelRuntime& runtime);
    ~ActivateScope();

   private:
    ApplicationModelSessionStorage* storage_{nullptr};
    ApplicationModelRuntime* runtime_{nullptr};
    ApplicationModelSessionStorage* previous_storage_{nullptr};
  };

 private:
  void FlushRamToDisk(ae::RamDomainStorage const& source,
                      std::set<ae::ObjId> const* only_ids,
                      bool clear_matching_session_entries);
  void FlushSessionLayers(std::set<ae::ObjId> const* only_ids);

  DirectoryDomainStorage& disk_user_;
  ae::IDomainStorage& disk_write_target_;
  ae::ObjId root_id_{};
  std::set<ae::ObjId> supply_constant_ids_;
  ae::RamDomainStorage embedded_backing_;
  OverlayDomainStorage persisted_user_overlay_;
  ae::RamDomainStorage session_overlay_;
  OverlayDomainStorage model_overlay_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_APPLICATION_MODEL_SESSION_STORAGE_H_
