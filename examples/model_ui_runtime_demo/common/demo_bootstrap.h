#ifndef APPTRAVERSE_DEMO_BOOTSTRAP_H_
#define APPTRAVERSE_DEMO_BOOTSTRAP_H_

#include <filesystem>
#include <memory>

#include "aether/clock.h"
#include "aether/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "demo_model.h"
#include "immutable_object_store.h"

namespace apptraverse {

struct DemoGraph {
  Application::ptr application;
  Window::ptr window_a_base;
  Window::ptr window_a;
  Window::ptr window_b_base;
  Window::ptr window_b;
  TextToolbar::ptr text_toolbar_base;
  TextToolbar::ptr text_toolbar;
  ColorToolbar::ptr color_toolbar_base;
  ColorToolbar::ptr color_toolbar;
  Chat::ptr chat_base;
  Chat::ptr chat;
  ImmutableString::ptr toolbar_text;
};

struct DemoRuntime {
  std::unique_ptr<DirectoryDomainStorage> storage;
  std::unique_ptr<ae::Domain> domain;
  DemoGraph graph;
  ImmutableObjectStore immutable_store;
};

DemoGraph BuildDemoGraph(ae::Domain& domain);
void CaptureDemoBases(DemoGraph& graph);
void ResolveDemoConstRefs(DemoGraph& graph, ImmutableObjectStore& store);

void DistillDemo(std::filesystem::path const& dir);
DemoRuntime LoadDemo(std::filesystem::path const& dir);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_BOOTSTRAP_H_
