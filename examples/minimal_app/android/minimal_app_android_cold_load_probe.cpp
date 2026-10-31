#include "minimal_app_android_cold_load_probe.h"

#include <cstdio>

#include "minimal_app_android_graph.h"
#include "minimal_app_controls.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

namespace apptraverse::example::minimal {
namespace {

void SetDetail(AndroidColdLoadLabelProbeResult& out, char const* text) {
  std::snprintf(out.detail, sizeof(out.detail), "%s", text);
}

bool ProbeOpenerLabelOnDomain(ae::Domain& domain, ae::ObjId const& expected_label_id,
                              AndroidColdLoadLabelProbeResult& out) {
  ae::Ptr<ae::Obj> const found = domain.Find(MinimalAuxiliaryOpenerId());
  if (!found) {
    SetDetail(out, "domain.Find(opener) null after LoadApplication");
    return false;
  }
  auto& opener = static_cast<MinimalAuxiliaryOpenerButton&>(*found);
  out.opener_label_valid = opener.label.is_valid();
  if (!out.opener_label_valid) {
    SetDetail(out, "opener.label invalid on domain object before Prepare");
    return false;
  }
  out.cold_label_obj_id = opener.label.id().id();
  if (out.cold_label_obj_id != expected_label_id.id()) {
    SetDetail(out, "opener.label ObjId changed after cold load");
    return false;
  }
  return true;
}

bool ProbeApplyLabelOnDomain(ae::Domain& domain, AndroidColdLoadLabelProbeResult& out) {
  ae::Ptr<ae::Obj> const found = domain.Find(MinimalApplyButtonId());
  if (!found) {
    SetDetail(out, "domain.Find(apply) null after LoadApplication");
    return false;
  }
  auto& apply = static_cast<MinimalApplyButton&>(*found);
  out.apply_label_valid = apply.label.is_valid();
  if (!out.apply_label_valid) {
    SetDetail(out, "apply.label invalid on domain object before Prepare");
    return false;
  }
  return true;
}

}  // namespace

AndroidColdLoadLabelProbeResult RunAndroidColdLoadLabelProbe(
    std::filesystem::path const& profile_dir) {
  AndroidColdLoadLabelProbeResult result;
  EnsureObjectRegistration();
  EnsureMinimalApplicationRegistration();

  std::error_code ec;
  std::filesystem::remove_all(profile_dir, ec);

#if !defined(APPTRAVERSE_ENABLE_DISTILLATION)
  SetDetail(result, "distillation disabled");
  return result;
#endif

  ae::ObjId built_label_id{};
  {
    DirectoryDomainStorage bootstrap_storage{profile_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildAndroidMinimalApplicationGraph(bootstrap_domain);
    bootstrap_app->auxiliary_opener.Load();
    bootstrap_app->apply_button.Load();
    if (!bootstrap_app->auxiliary_opener->label.is_valid()) {
      SetDetail(result, "bootstrap build: opener label invalid");
      return result;
    }
    built_label_id = bootstrap_app->auxiliary_opener->label.id();
    result.built_label_obj_id = built_label_id.id();
    PrepareMinimalApplicationForRun(*bootstrap_app);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }

  DirectoryDomainStorage model_storage{profile_dir};
  ae::Domain model_domain{model_storage};
  auto model_root =
      LoadApplication<MinimalApplication>(model_domain, MinimalApplicationRootId());
  if (!model_root) {
    SetDetail(result, "LoadApplication failed");
    return result;
  }

  model_root->apply_button.Load();
  model_root->auxiliary_opener.Load();
  if (!ProbeApplyLabelOnDomain(model_domain, result)) {
    return result;
  }
  if (!ProbeOpenerLabelOnDomain(model_domain, built_label_id, result)) {
    return result;
  }
  if (!model_root->auxiliary_opener->label.is_valid()) {
    SetDetail(result, "auxiliary_opener.Load(): label ref invalid before Prepare");
    return result;
  }
  if (model_root->auxiliary_opener->label.id().id() != built_label_id.id()) {
    SetDetail(result, "auxiliary_opener.Load(): label ObjId mismatch before Prepare");
    return result;
  }
  result.opener_label_valid = true;
  result.cold_label_obj_id = model_root->auxiliary_opener->label.id().id();

  result.ok = true;
  SetDetail(result, "cold load label probe OK");
  return result;
}

}  // namespace apptraverse::example::minimal
