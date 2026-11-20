#include "chat_ui_model.h"

#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
#  include <filesystem>

#  include "apptraverse/distill.h"
#  include "apptraverse/distilled_domain_export.h"

namespace apptraverse::chat::gui {
void BuildUiBacking(ae::RamDomainStorage& backing) {
  ae::Domain producer{backing};
  // BuildUi has its own identity range. Allocate the historical bases in
  // a separate stable range when finalizing the same supply on another run.
  ae::Domain::ObjectIdSequence bases{producer, ae::ObjId{10000},
                                     ae::ObjId{0x7fffffff}};
  auto root = BuildUi(producer);
  FinalizeDistilledGraph(*root);
  StabilizeDistilledPersistentSnapshot(*root);
  SaveDistilledRoot(*root);
}
int ExportUiSupply(std::filesystem::path const& path) {
  ae::RamDomainStorage backing;
  BuildUiBacking(backing);
  auto const objects = CollectStoredObjectIds(backing);
  std::vector<ae::ObjId> ids;
  for (auto id : objects)
    if (backing.IsConstantObject(id)) ids.push_back(id);
  return WriteEmbeddedDistilledSupplyHeader(
             path, "shared_chat_ui", "APPTRAVERSE_SHARED_CHAT_UI_SUPPLY_H_",
             RootId(), ids, backing, objects)
             ? 0
             : 1;
}
}  // namespace apptraverse::chat::gui
#endif
