#include "embedded_distilled_prefab_release_r0_supply.h"
#include "embedded_distilled_prefab_release_r2_supply.h"

int main() {
  // Observed supply ClassId for PrefabWindow in embedded R2 graph.
  std::uint32_t const window_class_id = 41031405u;

  bool r0_has_s1 = false;
  for (std::size_t i = 0;
       i < apptraverse::embedded_distilled::prefab_release_r0::kEmbeddedGraph
               .layer_count;
       ++i) {
    auto const& layer =
        apptraverse::embedded_distilled::prefab_release_r0::kEmbeddedGraph
            .layers[i];
    if (layer.class_id == window_class_id && layer.version == 1) {
      r0_has_s1 = true;
    }
  }

  bool r2_has_s1 = false;
  for (std::size_t i = 0;
       i < apptraverse::embedded_distilled::prefab_release_r2::kEmbeddedGraph
               .layer_count;
       ++i) {
    auto const& layer =
        apptraverse::embedded_distilled::prefab_release_r2::kEmbeddedGraph
            .layers[i];
    if (layer.class_id == window_class_id && layer.version == 1) {
      r2_has_s1 = true;
    }
  }

  if (r0_has_s1 || !r2_has_s1) {
    return 1;
  }
  return 0;
}
