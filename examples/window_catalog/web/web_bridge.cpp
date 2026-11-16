#include <cstdint>

#include <emscripten.h>

#include "web_app.h"

extern "C" {

EMSCRIPTEN_KEEPALIVE void AppTraverseWebAddCurrent() {
  apptraverse::WebApp::Instance().AddCurrent();
}

EMSCRIPTEN_KEEPALIVE void AppTraverseWebRemoveCurrent() {
  apptraverse::WebApp::Instance().RemoveCurrent();
}

EMSCRIPTEN_KEEPALIVE void AppTraverseWebSelectSurface(std::uint32_t obj_id) {
  apptraverse::WebApp::Instance().SelectSurface(obj_id);
}

EMSCRIPTEN_KEEPALIVE void AppTraverseWebReportPresentationSize(
    std::int32_t width, std::int32_t height) {
  apptraverse::WebApp::Instance().ReportPresentationSize(width, height);
}

EMSCRIPTEN_KEEPALIVE void AppTraverseWebStop() {
  apptraverse::WebApp::Instance().RequestStop();
}

EMSCRIPTEN_KEEPALIVE void AppTraverseWebSyncDone(int err) {
  apptraverse::WebApp::Instance().OnIndexedDbSyncDone(err);
}

EMSCRIPTEN_KEEPALIVE void AppTraverseWebRequestSync() {
  apptraverse::WebApp::Instance().RequestSyncFromJs();
}

}  // extern "C"
