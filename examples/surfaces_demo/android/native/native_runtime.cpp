#include "native_runtime.h"

#include <utility>
#include <vector>

#include "apptraverse/object_serialization.h"

#include "android_log.h"

namespace apptraverse::android {

NativeRuntime::NativeRuntime(std::filesystem::path state_dir,
                             SurfacesUiBridge ui_bridge)
    : ui_bridge_{std::move(ui_bridge)} {
  session_.state_dir = std::move(state_dir);
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });
}

void NativeRuntime::RunModel() {
  session_.Run([this](SurfacesPublicationKind kind) {
    ui_bridge_.PostPublication(kind == SurfacesPublicationKind::Initial ? 0 : 1);
  });
  LogMarker("SURFACES_STATE_SAVED");
  ui_bridge_.PostStopped();
}

void NativeRuntime::RequestStop() { session_.RequestStop(); }

void NativeRuntime::ConsumePublication(SurfacesPublicationKind kind) {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();

  if (kind == SurfacesPublicationKind::Initial) {
    ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
    ByteSource in;
    in.data = bytes.data();
    in.size = bytes.size();
    auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
    ui_application_ =
        Application::ptr::MakeFromThis(static_cast<Application*>(ui_root.get()));
    InitializePresenters(*ui_application_, nullptr, &*model_proxy_);
    LogMarker("SURFACES_UI_READY");
  } else {
    ApplySurfacesStructural(bytes, *ui_application_, ui_storage_, nullptr,
                            &*model_proxy_);
  }
  PublishPages();
}

SurfacePresenter::ptr NativeRuntime::FindLivePresenter(
    std::uint32_t surface_id) {
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    if (surface->obj_id.id() == surface_id) {
      return SurfacePresenter::ptr{surface->presenter};
    }
  }
  return {};
}

void NativeRuntime::AddFromSurface(std::uint32_t surface_id) {
  auto presenter = FindLivePresenter(surface_id);
  if (!presenter) {
    return;
  }
  presenter->AddClick();
}

void NativeRuntime::RemoveSurface(std::uint32_t surface_id) {
  auto presenter = FindLivePresenter(surface_id);
  if (!presenter) {
    return;
  }
  // Removing the last page ends the application; the Surface stays persisted.
  if (ui_application_->surfaces->surfaces.size() == 1) {
    LogMarker("SURFACES_LAST_PAGE_STOP");
    RequestStop();
    return;
  }
  presenter->RemoveClick();
}

void NativeRuntime::PublishPages() {
  auto const& surfaces = ui_application_->surfaces->surfaces;
  std::vector<std::int64_t> ids;
  std::vector<std::int32_t> numbers;
  ids.reserve(surfaces.size());
  numbers.reserve(surfaces.size());
  std::string marker = "SURFACES_PAGES numbers=";
  for (auto const& surface : surfaces) {
    ids.push_back(static_cast<std::int64_t>(surface->obj_id.id()));
    numbers.push_back(static_cast<std::int32_t>(surface->number));
    marker += std::to_string(surface->number);
    marker += ',';
  }
  LogMarker(marker + " count=" + std::to_string(surfaces.size()));
  ui_bridge_.PostPages(ids, numbers);
}

void NativeRuntime::UnloadUi() {
  UnloadPresenters(*ui_application_);
  ui_application_ = {};
  ui_domain_.reset();
  model_proxy_.reset();
  LogMarker("SURFACES_UI_UNLOADED");
}

}  // namespace apptraverse::android
