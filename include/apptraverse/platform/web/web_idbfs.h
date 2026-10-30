#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_IDBFS_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_IDBFS_H_

#include <functional>
#include <string>

namespace apptraverse::web {

using IdbfsCompletionCallback = std::function<void()>;

// Mount IDBFS at mount_path (browser main), syncfs(true), then invoke on_success.
// on_error on mount/populate failure (does not call on_success).
void MountIdbfsAndSyncLoad(std::string mount_path,
                           IdbfsCompletionCallback on_success,
                           IdbfsCompletionCallback on_error);

// syncfs(false) after model files were written (browser main).
void SyncIdbfsStore(IdbfsCompletionCallback on_success,
                    IdbfsCompletionCallback on_error);

}  // namespace apptraverse::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_IDBFS_H_
