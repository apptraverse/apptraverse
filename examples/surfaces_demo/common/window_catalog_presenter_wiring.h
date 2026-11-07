#ifndef APPTRAVERSE_WINDOW_CATALOG_PRESENTER_WIRING_H_
#define APPTRAVERSE_WINDOW_CATALOG_PRESENTER_WIRING_H_

#include "window_catalog_model.h"

#include "apptraverse/ui/ui_window_presenter.h"

namespace apptraverse {

void BindCatalogDesktopPresenter(ui::UiWindowPresenter& presenter,
                                 WindowCatalog& catalog);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_PRESENTER_WIRING_H_
