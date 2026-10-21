#ifndef APPTRAVERSE_UI_UI_BITMAP_IMAGE_H_
#define APPTRAVERSE_UI_UI_BITMAP_IMAGE_H_

#include <cstdint>
#include <vector>

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

namespace apptraverse::ui {

class UiWindowPresenter;
class UiBitmapImagePresenter;

class UiBitmapImage : public NodeFor<UiBitmapImage> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiBitmapImage", UiBitmapImage, Node,
                           0)

 protected:
  UiBitmapImage() = default;

 public:
  explicit UiBitmapImage(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height), AE_MMBR(bgra_bytes),
                    AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, width, height, bgra_bytes, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, width, height, bgra_bytes, presenter);
  }

  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<std::uint8_t> bgra_bytes;
  ae::ObjPtr<UiBitmapImagePresenter> presenter;
};

class UiBitmapImagePresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiBitmapImagePresenter",
                           UiBitmapImagePresenter, Presenter, 0)

 protected:
  UiBitmapImagePresenter() = default;

 public:
  explicit UiBitmapImagePresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(image), AE_MMBR(window_presenter), AE_MMBR(layout_x),
                    AE_MMBR(layout_y), AE_MMBR(layout_width),
                    AE_MMBR(layout_height))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, image, window_presenter, layout_x, layout_y, layout_width,
        layout_height);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, image, window_presenter, layout_x, layout_y, layout_width,
        layout_height);
  }

  UiBitmapImage::ptr image;
  ae::ObjPtr<UiWindowPresenter> window_presenter;
  std::int32_t layout_x{0};
  std::int32_t layout_y{0};
  std::int32_t layout_width{0};
  std::int32_t layout_height{0};

  bool ReadyForPresentation() const override;
  void OnModelChanged() override;
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_BITMAP_IMAGE_H_
