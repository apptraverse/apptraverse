#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_IDBFS_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_IDBFS_H_

#include <functional>
#include <cstdint>
#include <string>
#include <vector>

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

// Browser main thread, after MountIdbfsAndSyncLoad completed. Queue the complete
// file and its parent directories in the already-open IDBFS database and call
// transaction.commit() before returning. No later JS task is required to start
// the commit, unlike syncfs(false). True means commit was requested, not that
// the browser has reported durable completion (that callback may never run).
bool CommitIdbfsFileForPageLifecycle(std::string const& mount_path,
                                    std::string const& file_path,
                                    std::vector<std::uint8_t> const& bytes);

}  // namespace apptraverse::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_IDBFS_H_
