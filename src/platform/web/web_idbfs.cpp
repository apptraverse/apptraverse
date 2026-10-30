#include "apptraverse/platform/web/web_idbfs.h"

#include <utility>

#include <emscripten/em_js.h>
#include <emscripten/threading.h>

#include "apptraverse/noninteractive_crt.h"

namespace apptraverse::web {
namespace {

IdbfsCompletionCallback g_load_success;
IdbfsCompletionCallback g_load_error;
IdbfsCompletionCallback g_store_success;
IdbfsCompletionCallback g_store_error;

EM_JS(void, ApptraverseIdbfsMountAndLoad, (const char* mount_utf8), {
  var mount = UTF8ToString(mount_utf8);
  try {
    FS.mkdirTree(mount);
  } catch (e) {
    Module._ApptraverseWebIdbfsLoadFailed();
    return;
  }
  var mounted = false;
  for (var i = 0; i < FS.mounts.length; ++i) {
    if (FS.mounts[i].mountpoint === mount) {
      mounted = true;
      break;
    }
  }
  if (!mounted) {
    try {
      FS.mount(IDBFS, {}, mount);
    } catch (e2) {
      Module._ApptraverseWebIdbfsLoadFailed();
      return;
    }
  }
  FS.syncfs(true, function(err) {
    if (err) {
      Module._ApptraverseWebIdbfsLoadFailed();
    } else {
      Module._ApptraverseWebIdbfsLoadDone();
    }
  });
});

EM_JS(void, ApptraverseIdbfsSyncStore, (), {
  FS.syncfs(false, function(err) {
    if (err) {
      Module._ApptraverseWebIdbfsStoreFailed();
    } else {
      Module._ApptraverseWebIdbfsStoreDone();
    }
  });
});

void DispatchLoadSuccess() {
  IdbfsCompletionCallback cb = std::move(g_load_success);
  g_load_success = {};
  g_load_error = {};
  if (cb) {
    cb();
  }
}

void DispatchLoadError() {
  IdbfsCompletionCallback cb = std::move(g_load_error);
  g_load_success = {};
  g_load_error = {};
  if (cb) {
    cb();
  }
}

void DispatchStoreSuccess() {
  IdbfsCompletionCallback cb = std::move(g_store_success);
  g_store_success = {};
  g_store_error = {};
  if (cb) {
    cb();
  }
}

void DispatchStoreError() {
  IdbfsCompletionCallback cb = std::move(g_store_error);
  g_store_success = {};
  g_store_error = {};
  if (cb) {
    cb();
  }
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void ApptraverseWebIdbfsLoadDone() {
  DispatchLoadSuccess();
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWebIdbfsLoadFailed() {
  DispatchLoadError();
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWebIdbfsStoreDone() {
  DispatchStoreSuccess();
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWebIdbfsStoreFailed() {
  DispatchStoreError();
}

}  // extern "C"

void MountIdbfsAndSyncLoad(std::string mount_path,
                           IdbfsCompletionCallback on_success,
                           IdbfsCompletionCallback on_error) {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr("fatal: MountIdbfsAndSyncLoad off main thread\n");
    std::abort();
  }
  if (g_load_success || g_load_error) {
    WriteFatalStderr("fatal: overlapping IDBFS load\n");
    std::abort();
  }
  g_load_success = std::move(on_success);
  g_load_error = std::move(on_error);
  ApptraverseIdbfsMountAndLoad(mount_path.c_str());
}

void SyncIdbfsStore(IdbfsCompletionCallback on_success,
                    IdbfsCompletionCallback on_error) {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr("fatal: SyncIdbfsStore off main thread\n");
    std::abort();
  }
  if (g_store_success || g_store_error) {
    WriteFatalStderr("fatal: overlapping IDBFS store sync\n");
    std::abort();
  }
  g_store_success = std::move(on_success);
  g_store_error = std::move(on_error);
  ApptraverseIdbfsSyncStore();
}

}  // namespace apptraverse::web
