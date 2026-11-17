#ifndef APPTRAVERSE_EXAMPLE_RUNTIME_UI_SUPPLY_H_
#define APPTRAVERSE_EXAMPLE_RUNTIME_UI_SUPPLY_H_
#include <set>
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "runtime_ui_model.h"
namespace apptraverse::example::runtime_ui {
// Producer-only initial graph construction. Consumers compile the model alone.
RuntimeApplication::ptr BuildRuntimeUiGraph(
    ae::Domain& domain, bool dynamic, RuntimePresenterFactories factories);
void MakeRuntimeUiSupply(ae::RamDomainStorage& backing, bool dynamic,
                         RuntimePresenterFactories factories);
std::set<ae::ObjId> RuntimeUiSupplyConstants(
    ae::RamDomainStorage const& backing);
}  // namespace apptraverse::example::runtime_ui
#endif
