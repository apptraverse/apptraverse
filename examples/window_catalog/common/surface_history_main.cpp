// Specialized historical Surface schema/evolution CLI. Deliberately headless:
// current GUI profiles use the independent generic Surface S0 supply.
#include <filesystem>
#include <iostream>
#include <string_view>
#include "apptraverse/noninteractive_crt.h"
#include "surfaces_evolution_ops.h"
#include "surfaces_session_automation.h"
#include "surfaces_lifecycle.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_localization.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"
#include "apptraverse/ui/ui_presenters.h"

int RunHistoricalSession(std::filesystem::path const& directory, std::string const& spec) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  auto steps = apptraverse::ParseSurfacesSessionAutomation(spec);
  apptraverse::SurfacesModelSession session;
  session.state_dir = directory;
  session.Run([&](apptraverse::SurfacesPublicationKind kind) {
    {
      std::lock_guard<std::mutex> lock{session.mu};
      (void)session.channel.TakePublishedCopy();
    }
    session.cv.notify_all();
    if (kind != apptraverse::SurfacesPublicationKind::Initial) return;
    session.Post([&](ae::Domain& domain) {
      auto app = domain.Find(ae::ObjId{apptraverse::window_catalog::ToObjId(apptraverse::window_catalog::ObjId::Application)}).as<apptraverse::Application>();
      auto& surface = *app->surfaces->surfaces.front();
      for (auto const& step : steps) {
        using Op = apptraverse::SurfacesSessionAutomationOp;
        if (step.op == Op::Shutdown) break;
        if (step.op == Op::SetBounds) surface.SetDesktopBounds(step.x, step.y, step.width, step.height);
        if (step.op == Op::VerifyBounds && (surface.desktop_x != step.x || surface.desktop_y != step.y || surface.desktop_width != step.width || surface.desktop_height != step.height)) session.shutdown_exit_code = 1;
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
        if (step.op == Op::SetSuffix) surface.SetWindowTitleSuffix(step.text);
#endif
        if (step.op == Op::VerifyTitleSuffix) {
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
          auto title = apptraverse::FormatSurfaceWindowTitleWithSuffix(*app->window_title_prefix, app->locale->language_index(), surface);
#else
          auto title = apptraverse::FormatSurfaceWindowTitle(*app->window_title_prefix, app->locale->language_index(), surface.number);
#endif
          if (title.find(step.text) == std::string::npos) session.shutdown_exit_code = 1;
        }
      }
      session.RequestStop();
    });
  });
  return session.shutdown_exit_code;
}
int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();
  std::filesystem::path directory{"surfaces_runtime_state"};
  std::string operation;
  std::string automation;
  bool inspect = false;
  for (int i = 1; i < argc; ++i) {
    auto argument = std::string_view{argv[i]};
    if (argument == "--state-dir" && i + 1 < argc) directory = argv[++i];
    else if (argument == "--evolution-op" && i + 1 < argc) operation = argv[++i];
    else if (argument == "--session-inspect") inspect = true;
    else if (argument == "--session-automation" && i + 1 < argc) automation = argv[++i];
    else return 2;
  }
  if (inspect) return apptraverse::RunSurfacesSessionInspect(directory);
  if (!operation.empty()) return apptraverse::RunSurfacesEvolutionOp(directory, operation);
  if (!automation.empty()) return RunHistoricalSession(directory, automation);
  std::cerr << "Expected --session-inspect or --evolution-op\n";
  return 2;
}
