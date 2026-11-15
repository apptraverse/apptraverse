#include "apptraverse/platform/web/web_idbfs.h"
#include <cstdlib>

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
  var mount = UTF8ToString(Number(mount_utf8));
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

EM_JS(int, ApptraverseIdbfsCommitLifecycleFile,
      (const char* mount_utf8, const char* file_utf8,
       const std::uint8_t* bytes, std::size_t size), {
  var mount = PATH.normalize(UTF8ToString(Number(mount_utf8)));
  var file = PATH.normalize(UTF8ToString(Number(file_utf8)));
  if (!file.startsWith(mount + '/')) return 0;
  var db = IDBFS.dbs[mount];
  if (!db) return 0;
  var transaction;
  try {
    FS.mkdirTree(PATH.dirname(file));
    // IndexedDB cannot store a view backed by Wasm's SharedArrayBuffer.
    FS.writeFile(file, new Uint8Array(HEAPU8.subarray(Number(bytes), Number(bytes) + Number(size))));
    // IDBFS persists files in FILE_DATA. Its JS implementation no longer
    // exposes DB_STORE_NAME as an IDBFS object property.
    transaction = db.transaction(['FILE_DATA'], 'readwrite');
    var store = transaction.objectStore('FILE_DATA');
    var path = file;
    while (path !== mount) {
      var stat = FS.stat(path);
      var entry = { timestamp: stat.mtime, mode: stat.mode };
      if (path === file) entry.contents = FS.readFile(file);
      store.put(entry, path);
      path = PATH.dirname(path);
    }
    transaction.onabort = function() {
      console.error('IDBFS lifecycle USER commit failed', transaction.error);
    };
    // Queue all data before ending the lifecycle event, without an asynchronous
    // database-open/enumeration/request callback between here and commit().
    transaction.commit();
    return 1;
  } catch (error) {
    if (transaction) transaction.abort();
    console.error('Lifecycle USER commit failed', error);
    return 0;
  }
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

bool CommitIdbfsFileForPageLifecycle(std::string const& mount_path,
                                    std::string const& file_path,
                                    std::vector<std::uint8_t> const& bytes) {
  if (!emscripten_is_main_browser_thread() ||
      file_path.compare(0, mount_path.size() + 1, mount_path + "/") != 0) {
    return false;
  }
  return ApptraverseIdbfsCommitLifecycleFile(
      mount_path.c_str(), file_path.c_str(), bytes.data(), bytes.size()) != 0;
}

}  // namespace apptraverse::web
