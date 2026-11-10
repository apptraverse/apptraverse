#ifndef APPTRAVERSE_WINDOW_CATALOG_DISTILLATION_H_
#define APPTRAVERSE_WINDOW_CATALOG_DISTILLATION_H_

#include <filesystem>
#include <set>

#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

struct WindowCatalogInitialSupply {
  std::filesystem::path backing_directory;
  std::set<ae::ObjId> constant_ids;
};

// Debug-only preparation. Each invocation owns a new directory of ordinary
// object/class/version files; the header is another representation for embedding.
WindowCatalogInitialSupply DistillWindowCatalog(
    std::filesystem::path const& output_directory);

}  // namespace apptraverse

#endif
