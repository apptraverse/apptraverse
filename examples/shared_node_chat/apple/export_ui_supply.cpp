#include "AppleComposition.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/noninteractive_crt.h"
int main(int argc,char** argv){
  if(argc!=2)return 2;
  using namespace apptraverse;
  using namespace apptraverse::chat::gui;
  EnableNoninteractiveCrt();
  ConfigureAppleComposition({}); // Producer has no OS observations; runtime supplies them in prepare before the initial publication.
  ae::RamDomainStorage backing;ae::Domain domain{backing};
  auto root=BuildUi(domain);
  FinalizeDistilledGraph(*root);StabilizeDistilledPersistentSnapshot(*root);SaveDistilledRoot(*root);
  std::vector<ae::ObjId> constants;
  for(auto id:CollectStoredObjectIds(backing))if(backing.IsConstantObject(id))constants.push_back(id);
  return WriteEmbeddedDistilledSupplyHeader(argv[1],"shared_chat_ui","APPTRAVERSE_SHARED_CHAT_UI_SUPPLY_H_",
      RootId(),constants,backing,CollectStoredObjectIds(backing))?0:1;
}
