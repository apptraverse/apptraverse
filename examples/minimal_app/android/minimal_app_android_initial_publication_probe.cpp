#include "minimal_app_android_initial_publication_probe.h"

#include <cstdio>
#include <cstring>

#include "minimal_app_android_graph.h"
#include "minimal_app_controls.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::example::minimal {
namespace {

void SetStage(AndroidInitialPublicationLabelProbeResult& out, char const* stage) {
  std::snprintf(out.stage, sizeof(out.stage), "%s", stage);
}

void SetDetail(AndroidInitialPublicationLabelProbeResult& out, char const* text) {
  std::snprintf(out.detail, sizeof(out.detail), "%s", text);
}

bool OpenerLabelValidOnDomain(ae::Domain& domain) {
  ae::Ptr<ae::Obj> const found = domain.Find(MinimalAuxiliaryOpenerId());
  if (!found) {
    return false;
  }
  auto& opener = static_cast<MinimalAuxiliaryOpenerButton&>(*found);
  return opener.label.is_valid();
}

}  // namespace

AndroidInitialPublicationLabelProbeResult RunAndroidInitialPublicationLabelProbe(
    std::filesystem::path const& profile_dir) {
  AndroidInitialPublicationLabelProbeResult result;
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

  std::error_code ec;
  std::filesystem::remove_all(profile_dir, ec);

#if !defined(APPTRAVERSE_ENABLE_DISTILLATION)
  SetStage(result, "distillation");
  SetDetail(result, "distillation disabled");
  return result;
#endif

  {
    DirectoryDomainStorage bootstrap_storage{profile_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildAndroidMinimalApplicationGraph(bootstrap_domain);
    bootstrap_app->auxiliary_opener.Load();
    if (!bootstrap_app->auxiliary_opener->label.is_valid()) {
      SetStage(result, "bootstrap");
      SetDetail(result, "bootstrap opener label invalid");
      return result;
    }
    PrepareMinimalApplicationForRun(*bootstrap_app);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }

  DirectoryDomainStorage model_storage{profile_dir};
  ae::Domain model_domain{model_storage};
  auto model_root =
      LoadApplication<MinimalApplication>(model_domain, MinimalApplicationRootId());
  if (!model_root) {
    SetStage(result, "cold_load");
    SetDetail(result, "LoadApplication failed");
    return result;
  }
  model_root->auxiliary_opener.Load();
  if (!OpenerLabelValidOnDomain(model_domain)) {
    SetStage(result, "after_cold_load");
    SetDetail(result, "model opener label invalid before Prepare");
    return result;
  }

  PrepareMinimalApplicationForRun(*model_root);
  ae::ObjId const model_label_id = model_root->auxiliary_opener->label.id();
  if (!OpenerLabelValidOnDomain(model_domain)) {
    SetStage(result, "after_prepare");
    SetDetail(result, "model opener label invalid after Prepare");
    return result;
  }

  ByteSink initial;
  SerializeInitialPublication(*model_root, initial);

  if (!InitialPublicationPayloadHasLayer(initial.bytes, MinimalAuxiliaryOpenerId(),
                                       ui::UiPushButton::kClassId)) {
    SetStage(result, "payload_missing_opener_label_layer");
    SetDetail(result, "SerializeInitialPublication missing UiPushButton layer for opener");
    return result;
  }
  if (!InitialPublicationPayloadContainsObject(initial.bytes, model_label_id)) {
    SetStage(result, "payload_missing_label_object");
    SetDetail(result, "SerializeInitialPublication missing localization label object");
    return result;
  }

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource in;
  in.data = initial.bytes.data();
  in.size = initial.bytes.size();
  InitialPublicationLoadDiagnostics diagnostics;
  ae::Ptr<ae::Obj> ui_root =
      TryLoadInitialPublication(in, ui_domain, ui_storage, &diagnostics);

  if (!ui_root) {
    SetStage(result, "load_initial_publication");
    char message[512];
    FormatInitialPublicationLoadDiagnostics(diagnostics, message, sizeof(message));
    SetDetail(result, message);
    return result;
  }

  auto& ui_app = static_cast<MinimalApplication&>(*ui_root);
  if (!OpenerLabelValidOnDomain(ui_domain)) {
    SetStage(result, "ui_domain_opener");
    SetDetail(result, "ui domain opener label invalid after hydrate");
    return result;
  }

  ui_app.presenters.auxiliary_opener.Load();
  ui_app.presenters.auxiliary_opener->button.Load();
  if (ui_app.presenters.auxiliary_opener->button.id() !=
      MinimalAuxiliaryOpenerId()) {
    SetStage(result, "presenter_identity");
    SetDetail(result, "presenter button ObjId != auxiliary_opener");
    return result;
  }

  ui_app.presenters.auxiliary_opener->button->label.Load();
  if (!ui_app.presenters.auxiliary_opener->button->label) {
    SetStage(result, "presenter_mirror_label");
    SetDetail(result, "presenter button label invalid before SyncFromMirror");
    return result;
  }
  if (ui_app.presenters.auxiliary_opener->button->label->Resolve(0) !=
      "Open Detail") {
    SetStage(result, "presenter_mirror_label");
    SetDetail(result, "presenter button label text mismatch");
    return result;
  }

  result.ok = true;
  SetStage(result, "ok");
  SetDetail(result, "initial publication label probe OK");
  return result;
}

}  // namespace apptraverse::example::minimal
