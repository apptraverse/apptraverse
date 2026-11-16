#include <filesystem>
#include "window_catalog_graph.h"
#include "window_catalog_distillation.h"

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  apptraverse::EnsureWindowCatalogGraphRegistration();
  apptraverse::DistillWindowCatalog(std::filesystem::absolute(argv[1]));
  return 0;
}
