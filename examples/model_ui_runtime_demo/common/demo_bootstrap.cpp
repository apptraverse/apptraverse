#include "demo_bootstrap.h"

#include <cassert>
#include <filesystem>

#include "demo_ids.h"
#include "demo_log.h"

namespace apptraverse {
namespace {

using demo::DemoObjId;
using demo::ToObjId;

void InitWindowBounds(Window& window, std::int32_t left, std::int32_t top,
                      std::int32_t right, std::int32_t bottom) {
  window.left = left;
  window.top = top;
  window.right = right;
  window.bottom = bottom;
  window.dpi = demo::kDefaultDpi;
  window.client_width = (right - left) - 16;
  window.client_height = (bottom - top) - 39;
  if (window.client_width < 1) {
    window.client_width = 1;
  }
  if (window.client_height < 1) {
    window.client_height = 1;
  }
}

void InitDerivedChildren(Window& window) {
  if (window.text_toolbar) {
    window.text_toolbar.Load();
    window.text_toolbar->x = 0;
    window.text_toolbar->y = 0;
    window.text_toolbar->width = window.client_width;
    window.text_toolbar->height = demo::kTextToolbarHeight;
  }
  if (window.color_toolbar) {
    window.color_toolbar.Load();
    window.color_toolbar->x = 0;
    window.color_toolbar->y = demo::kTextToolbarHeight;
    window.color_toolbar->width = window.client_width;
    window.color_toolbar->height = demo::kColorToolbarHeight;
  }
  if (window.chat) {
    window.chat.Load();
    std::int32_t const toolbar_h =
        demo::kTextToolbarHeight + demo::kColorToolbarHeight;
    window.chat->width = (window.client_width * 2) / 3;
    if (window.chat->width < 1) {
      window.chat->width = 1;
    }
    window.chat->x = (window.client_width - window.chat->width) / 2;
    window.chat->y = toolbar_h;
    window.chat->height = window.client_height - toolbar_h;
    if (window.chat->height < 1) {
      window.chat->height = 1;
    }
  }
}

}  // namespace

DemoGraph BuildDemoGraph(ae::Domain& domain) {
  DemoGraph graph;
  graph.application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::Application)));
  graph.window_a_base = Window::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::WindowABase)));
  graph.window_a = Window::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::WindowA)));
  graph.window_b_base = Window::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::WindowBBase)));
  graph.window_b = Window::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::WindowB)));
  graph.text_toolbar_base = TextToolbar::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::TextToolbarBase)));
  graph.text_toolbar = TextToolbar::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::TextToolbar)));
  graph.color_toolbar_base = ColorToolbar::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::ColorToolbarBase)));
  graph.color_toolbar = ColorToolbar::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::ColorToolbar)));
  graph.chat_base = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::ChatBase)));
  graph.chat = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::Chat)));
  graph.toolbar_text = ImmutableString::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::ToolbarText)));
  graph.toolbar_text->bytes = demo::kToolbarTextBytes;

  InitWindowBounds(*graph.window_a, demo::kWindowALeft, demo::kWindowATop,
                   demo::kWindowARight, demo::kWindowABottom);
  InitWindowBounds(*graph.window_b, demo::kWindowBLeft, demo::kWindowBTop,
                   demo::kWindowBRight, demo::kWindowBBottom);

  graph.window_a->base = graph.window_a_base;
  graph.window_b->base = graph.window_b_base;
  graph.text_toolbar->base = graph.text_toolbar_base;
  graph.color_toolbar->base = graph.color_toolbar_base;
  graph.chat->base = graph.chat_base;

  graph.text_toolbar->text_id = graph.toolbar_text.id();
  graph.window_b->text_toolbar = graph.text_toolbar;
  graph.window_b->color_toolbar = graph.color_toolbar;
  graph.window_b->chat = graph.chat;
  graph.application->window_a = graph.window_a;
  graph.application->window_b = graph.window_b;
  graph.application->toolbar_text = graph.toolbar_text;

  InitDerivedChildren(*graph.window_b);
  return graph;
}

void CaptureDemoBases(DemoGraph& graph) {
  graph.window_a->CaptureBaseState();
  graph.text_toolbar->CaptureBaseState();
  graph.color_toolbar->CaptureBaseState();
  graph.chat->CaptureBaseState();
  graph.window_b->CaptureBaseState();
}

void ResolveDemoConstRefs(DemoGraph& graph, ImmutableObjectStore& store) {
  assert(graph.toolbar_text);
  graph.toolbar_text.Load();
  store.Add(*graph.toolbar_text);
  if (graph.text_toolbar) {
    graph.text_toolbar.Load();
    graph.text_toolbar->text.id = graph.text_toolbar->text_id;
    graph.text_toolbar->text.ptr = store.Find(graph.text_toolbar->text_id);
    assert(graph.text_toolbar->text.ptr == &*graph.toolbar_text);
  }
}

void DistillDemo(std::filesystem::path const& dir) {
  std::filesystem::remove_all(dir);
  DirectoryDomainStorage storage{dir};
  ae::Domain domain{ae::Now(), storage};
  auto graph = BuildDemoGraph(domain);
  CaptureDemoBases(graph);
  graph.application.Save();  // runtime-save-ok: distill
  demo::DemoLog("distilled model_ui_runtime_demo to " + dir.string());
}

DemoRuntime LoadDemo(std::filesystem::path const& dir) {
  DemoRuntime runtime;
  runtime.storage = std::make_unique<DirectoryDomainStorage>(dir);
  runtime.domain =
      std::make_unique<ae::Domain>(ae::Now(), *runtime.storage);
  auto& domain = *runtime.domain;
  runtime.graph.application = Application::ptr::Declare(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::Application)));
  runtime.graph.application.Load();
  assert(runtime.graph.application);
  runtime.graph.window_a = runtime.graph.application->window_a;
  runtime.graph.window_b = runtime.graph.application->window_b;
  runtime.graph.toolbar_text = runtime.graph.application->toolbar_text;
  runtime.graph.window_a.Load();
  runtime.graph.window_b.Load();
  runtime.graph.toolbar_text.Load();
  runtime.graph.text_toolbar = runtime.graph.window_b->text_toolbar;
  runtime.graph.color_toolbar = runtime.graph.window_b->color_toolbar;
  runtime.graph.chat = runtime.graph.window_b->chat;
  runtime.graph.text_toolbar.Load();
  runtime.graph.color_toolbar.Load();
  runtime.graph.chat.Load();
  ResolveDemoConstRefs(runtime.graph, runtime.immutable_store);
  return runtime;
}

}  // namespace apptraverse
