#include "demo_bootstrap.h"

#include <filesystem>
#include <stdexcept>
#include <string>

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
  window.client_width = (right - left) - 16;
  window.client_height = (bottom - top) - 39;
  if (window.client_width < 1) {
    window.client_width = 1;
  }
  if (window.client_height < 1) {
    window.client_height = 1;
  }
}

}  // namespace

Application::ptr BuildDemoGraph(ae::Domain& domain) {
  auto application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::Application)));
  auto window_a = PaintWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::PaintWindow)));
  auto window_b = LayoutWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::LayoutWindow)));
  auto text_toolbar = TextToolbar::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::TextToolbar)));
  auto color_toolbar = ColorToolbar::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::ColorToolbar)));
  auto center_strip = CenterStrip::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::CenterStrip)));
  auto toolbar_text = ImmutableString::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(DemoObjId::ToolbarText)));
  toolbar_text->bytes = demo::kToolbarTextBytes;

  InitWindowBounds(*window_a, demo::kWindowALeft, demo::kWindowATop,
                   demo::kWindowARight, demo::kWindowABottom);
  InitWindowBounds(*window_b, demo::kWindowBLeft, demo::kWindowBTop,
                   demo::kWindowBRight, demo::kWindowBBottom);

  text_toolbar->height = demo::kTextToolbarHeight;
  text_toolbar->text = toolbar_text;
  color_toolbar->height = demo::kColorToolbarHeight;
  center_strip->width_numerator = 2;
  center_strip->width_denominator = 3;
  center_strip->fill_color = demo::kCenterStripFill;

  window_b->text_toolbar = text_toolbar;
  window_b->color_toolbar = color_toolbar;
  window_b->center_strips.push_back(center_strip);
  application->window_a = window_a;
  application->window_b = window_b;
  return application;
}

void DistillModel(std::filesystem::path const& dir) {
  std::filesystem::remove_all(dir);
  DirectoryDomainStorage storage{dir};
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildDemoGraph(domain);
  FinalizeDistilledGraph(*application);
  SaveDistilledRoot(*application);  // runtime-save-ok: distill
  demo::DemoLog("distilled model_ui_runtime_demo model to " + dir.string());
}

DemoRuntime LoadDemoModel(std::filesystem::path const& dir) {
  if (!std::filesystem::exists(dir)) {
    throw std::runtime_error("distilled state missing: " + dir.string());
  }
  DemoRuntime runtime;
  runtime.storage = std::make_unique<DirectoryDomainStorage>(dir);
  runtime.ui_storage = std::make_unique<OverlayDomainStorage>(*runtime.storage);
  runtime.model_domain =
      std::make_unique<ae::Domain>(ae::Now(), *runtime.storage);
  runtime.ui_domain =
      std::make_unique<ae::Domain>(ae::Now(), *runtime.ui_storage);
  runtime.application = LoadApplication<Application>(
      *runtime.model_domain, ae::ObjId{ToObjId(DemoObjId::Application)});
  return runtime;
}

}  // namespace apptraverse
