#include "minimal_app_android_cold_reload_apply_probe.h"

#include <cstdio>

#include "minimal_application.h"
#include "minimal_app_prepare.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

namespace apptraverse::example::minimal {
namespace {

void SetDetail(AndroidColdReloadApplyProbeResult& out, char const* text) {
  std::snprintf(out.detail, sizeof(out.detail), "%s", text);
}

}  // namespace

AndroidColdReloadApplyProbeResult RunAndroidColdReloadApplyStateProbe(
    std::filesystem::path const& profile_dir) {
  AndroidColdReloadApplyProbeResult result;
  EnsureObjectRegistration();
  EnsureMinimalApplicationRegistration();

  DirectoryDomainStorage storage{profile_dir};
  ae::Domain domain{storage};
  MinimalApplication::ptr application = MinimalApplication::ptr::Declare(
      ae::CreateWith{domain}.with_id(MinimalApplicationRootId()));
  ae::DomainGraph graph{&domain};
  ae::seri::SeriResult const load =
      graph.LoadRoot(MinimalApplicationRootId(), application.cached());
  if (load.IsErr()) {
    SetDetail(result, "LoadRoot failed");
    return result;
  }
  if (!application) {
    SetDetail(result, "application root missing after LoadRoot");
    return result;
  }

  application->row_list.Load();
  application->apply_button.Load();
  application->row_list->window_title.Load();
  if (!application->row_list || !application->apply_button ||
      !application->row_list->window_title) {
    SetDetail(result, "required refs missing after Load");
    return result;
  }

  result.selection_index = application->row_list->selection_index;
  result.apply_enabled = application->apply_button->enabled();
  std::string const title =
      application->row_list->window_title->Resolve(0);
  std::snprintf(result.title, sizeof(result.title), "%s", title.c_str());

  if (result.selection_index != -1) {
    SetDetail(result, "selection_index expected -1 after apply save");
    return result;
  }
  if (result.apply_enabled) {
    SetDetail(result, "apply_button expected disabled after apply save");
    return result;
  }
  if (std::string_view{result.title} != "Applied") {
    SetDetail(result, "window title expected Applied");
    return result;
  }

  result.ok = true;
  SetDetail(result, "cold reload apply probe OK");
  return result;
}

}  // namespace apptraverse::example::minimal
