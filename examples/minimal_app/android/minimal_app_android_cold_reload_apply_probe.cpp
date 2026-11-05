#include "minimal_app_android_cold_reload_apply_probe.h"

#include <cstdio>

#include "minimal_application.h"
#include "minimal_app_android_embedded_profile.h"
#include "minimal_app_controls.h"

#include "apptraverse/embedded_distilled_domain.h"

#include "embedded_distilled_minimal_app_supply.h"

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

  MinimalAppEmbeddedProfile profile{profile_dir};
  ae::Domain domain{profile.overlay};
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

AndroidColdReloadApplyProbeResult RunAndroidColdReloadProfileStateProbe(
    std::filesystem::path const& profile_dir, int expected_selection_index,
    bool expected_apply_enabled, char const* expected_title) {
  AndroidColdReloadApplyProbeResult result;
  EnsureObjectRegistration();
  EnsureMinimalApplicationRegistration();

  MinimalAppEmbeddedProfile profile{profile_dir};
  ae::Domain domain{profile.overlay};
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

  if (result.selection_index != expected_selection_index) {
    SetDetail(result, "selection_index mismatch");
    return result;
  }
  if (result.apply_enabled != expected_apply_enabled) {
    SetDetail(result, "apply_button enabled mismatch");
    return result;
  }
  if (std::string_view{result.title} != expected_title) {
    SetDetail(result, "window title mismatch");
    return result;
  }

  result.ok = true;
  SetDetail(result, "profile state probe OK");
  return result;
}

AndroidColdReloadApplyProbeResult RunAndroidEmbeddedBackingProbe(
    std::filesystem::path const& profile_dir,
    std::filesystem::path const& empty_user_dir) {
  AndroidColdReloadApplyProbeResult result;
  EnsureObjectRegistration();
  EnsureMinimalApplicationRegistration();

  {
    DirectoryDomainStorage user{profile_dir};
    if (user.Enumerate(MinimalApplyButtonId()).empty()) {
      SetDetail(result, "USER holds no applied Apply button layer");
      return result;
    }
    EmbeddedDistilledSupply const& supply =
        embedded_distilled::minimal::kEmbeddedGraph.supply_constant_ids;
    for (std::size_t i = 0; i < supply.count; ++i) {
      if (!user.Enumerate(ae::ObjId{supply.ids[i]}).empty()) {
        SetDetail(result, "USER holds a supply constant layer");
        return result;
      }
    }
  }

  std::error_code ec;
  std::filesystem::remove_all(empty_user_dir, ec);
  result = RunAndroidColdReloadProfileStateProbe(empty_user_dir, -1, true,
                                                 "Minimal App");
  std::filesystem::remove_all(empty_user_dir, ec);
  if (result.ok) {
    SetDetail(result, "embedded backing probe OK");
  }
  return result;
}

}  // namespace apptraverse::example::minimal
