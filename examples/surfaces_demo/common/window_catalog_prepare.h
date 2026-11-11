#ifndef APPTRAVERSE_WINDOW_CATALOG_PREPARE_H_
#define APPTRAVERSE_WINDOW_CATALOG_PREPARE_H_

namespace apptraverse {

class WindowCatalog;

void PrepareWindowCatalogForRun(WindowCatalog& catalog);
void PrepareWindowCatalogForDesktopRun(WindowCatalog& catalog);
void PrepareWindowCatalogForPagedRun(WindowCatalog& catalog);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_PREPARE_H_
