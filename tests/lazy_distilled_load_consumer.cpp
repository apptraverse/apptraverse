#include <iostream>

#include "apptraverse/object_macros.h"
#include "distilled_lazy_graph_bootstrap.h"
#include "embedded_distilled_lazy_doc_supply.h"

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::EnsureLazyDistilledGraphRegistration();
  apptraverse::test::RunEmbeddedLazyDistilledConsumerChecks(
      apptraverse::embedded_distilled::lazy_doc::kEmbeddedGraph);
  std::cout << "lazy_distilled_load_consumer OK\n";
  return 0;
}
