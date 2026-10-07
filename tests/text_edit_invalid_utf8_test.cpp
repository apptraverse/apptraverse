#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/control_text_edit.h"
#include "apptraverse/runtime_node.h"

int main() {
  apptraverse::EnsureTextEditControlRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto edit = apptraverse::TextEdit::ptr::Create(ae::CreateWith{domain});
  apptraverse::InitializeRuntimeNode(*edit);
  edit->RequestInsert("\xFF");
  return 0;
}
