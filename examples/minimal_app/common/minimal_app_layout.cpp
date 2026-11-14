#include "minimal_application.h"
#include "minimal_app_window_events.h"

#include <algorithm>

namespace apptraverse::example::minimal {
namespace {
template <class P>
void Place(P& presenter, int x, int y, int width, int height) {
  presenter.layout_x = x;
  presenter.layout_y = y;
  presenter.layout_width = width;
  presenter.layout_height = height;
}
}

void MinimalWindow::OnClientSizeChanged() { application->ReflowWindow(*this); }

void MinimalApplication::ReflowWindow(ui::UiDesktopWindow& window) {
  auto px = [this](int dp) {
    return static_cast<int>((static_cast<std::int64_t>(dp) * viewport.density_dpi + 80) / 160);
  };
  int const width = window.client_width(), height = window.client_height();
  int const margin = std::min({px(12), (width - 1) / 4, (height - 1) / 8});
  int const gap = margin;
  int const inner = std::max(1, width - 2 * margin);
  bool const stacked = inner < px(280);
  int const button_height = std::min(px(48), std::max(1, (height - 4 * margin) / (stacked ? 4 : 3)));
  int const button_width = stacked ? inner : std::max(1, (inner - gap) / 2);
  int const second_x = stacked ? margin : margin + button_width + gap;
  int const second_y = stacked ? margin + button_height + gap : margin;
  int const list_y = second_y + button_height + gap;
  bool const main = window.obj_id == main_window.id();
  auto& apply = main ? *presenters.apply_button : *auxiliary.presenters.apply_button;
  auto& action = main ? *presenters.auxiliary_opener : *auxiliary.presenters.closer;
  auto& list = main ? *presenters.row_list : *auxiliary.presenters.row_list;
  Place(apply, margin, margin, button_width, button_height);
  Place(action, second_x, second_y, button_width, button_height);
  Place(list, margin, list_y, inner, std::max(1, height - list_y - margin));
  NoteMaterializedChange();
}

void MinimalApplication::LayoutHostedWindows() {
  if (viewport.hosted) {
    main_window->RequestFrame(0, 0, viewport.width, viewport.height);
    if (auxiliary_presented_open) {
      int const inset = viewport.single_page ? 0 : 24;
      auxiliary.window->RequestFrame(inset, inset,
          viewport.width - 2 * inset, viewport.height - 2 * inset);
    }
  }
  ReflowWindow(*main_window);
  if (auxiliary_presented_open) ReflowWindow(*auxiliary.window);
}

void MinimalApplication::SubmitViewport(int width, int height, int dpi,
                                         bool requires_single_page) {
  if (width <= 0 || height <= 0 || dpi <= 0) return; // Native view not laid out yet.
  bool const pages = requires_single_page || static_cast<std::int64_t>(width) * 160 < 600ll * dpi;
  if (viewport.hosted && viewport.width == width && viewport.height == height &&
      viewport.density_dpi == dpi && viewport.single_page == pages) return;
  auto event = MinimalViewportEvent::ptr::Create(ae::CreateWith{*domain});
  event->width = width; event->height = height; event->dpi = dpi;
  event->requires_single_page = requires_single_page;
  Commit(std::move(event));
}

void MinimalApplication::Apply(MinimalViewportEvent const& event) {
  viewport.width = event.width;
  viewport.height = event.height;
  viewport.density_dpi = event.dpi;
  viewport.hosted = true;
  viewport.single_page = event.requires_single_page ||
      static_cast<std::int64_t>(event.width) * 160 < 600ll * event.dpi;
  LayoutHostedWindows();
}

void MinimalApplication::SubmitDesktopEnvironment(int dpi) {
  if (!viewport.hosted && !viewport.single_page && viewport.density_dpi == dpi) return;
  auto event = MinimalDesktopEnvironmentEvent::ptr::Create(ae::CreateWith{*domain});
  event->dpi = dpi;
  Commit(std::move(event));
}
void MinimalApplication::Apply(MinimalDesktopEnvironmentEvent const& event) {
  viewport.density_dpi = event.dpi;
  viewport.hosted = false;
  viewport.single_page = false;
  LayoutHostedWindows();
}
}  // namespace apptraverse::example::minimal
