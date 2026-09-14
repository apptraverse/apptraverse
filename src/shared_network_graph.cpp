#include "apptraverse/shared_network_graph.h"

#include <cassert>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/registry.h"

namespace apptraverse {
namespace {

void TransferRamObject(ae::RamDomainStorage const& src, ae::ObjId obj_id,
                       ae::IDomainStorage& dst) {
  auto const it = src.state.find(obj_id);
  if (it == src.state.end() || !it->second.has_value()) {
    return;
  }
  for (auto const& [class_id, versions] : *it->second) {
    for (auto const& [version, data] : versions) {
      auto writer = dst.Store(ae::DomainQuery{obj_id, class_id, version});
      assert(writer != nullptr);
      if (!data.empty()) {
        auto const result = writer->Write(
            ae::seri::DataWriteTag{data.data(), data.size()});
        assert(result);
        (void)result;
      }
    }
  }
}

}  // namespace

void CopyNetworkSharedObjectGraph(ae::Obj const& root,
                                  ae::IDomainStorage& target_storage) {
  assert(root.domain != nullptr);

  ae::RamDomainStorage scratch;
  ae::Domain scratch_domain{scratch};
  {
    ae::DomainGraph graph{&scratch_domain,
                          ae::GraphSerializationScope::NetworkShared};

    auto ptr = root.domain->Find(root.obj_id);
    assert(ptr);
    auto* factory =
        ae::Registry::GetRegistry().FindFactory(root.GetClassId());
    assert(factory != nullptr);
    assert(factory->save != nullptr);
    factory->save(&graph, ptr, root.obj_id);
  }

  for (auto const& [obj_id, classes] : scratch.state) {
    if (!classes.has_value()) {
      continue;
    }
    TransferRamObject(scratch, obj_id, target_storage);
  }
}

void CopySharedNetworkGraph(SharedNode::ptr source,
                            ae::Domain& target_domain,
                            ae::IDomainStorage& target_storage) {
  assert(source.is_valid());
  assert(source.is_loaded());
  (void)target_domain;
  CopyNetworkSharedObjectGraph(*source, target_storage);
}

}  // namespace apptraverse
