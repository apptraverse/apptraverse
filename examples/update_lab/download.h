#pragma once
#include "model.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace update_lab {
struct TransferRequest {
  DownloadSnapshot snapshot;
  InstalledImage installed;
  unsigned current_executable, bytes_per_second;
  bool preview, prepared;
};
struct TransferResult {
  std::uint64_t epoch;
  Intent intent;
  UpdatePlan plan;
  Bytes executable;
  std::uint64_t resumed_bytes{};
  bool preview{};
  std::string error;
  std::optional<std::uint64_t> data_only_remaining{};
};
class TransferWorker {
 public:
  using Report = std::function<bool(DownloadSnapshot)>;
  using Ready = std::function<void(TransferResult)>;
  TransferWorker(fs::path profile, fs::path local_source, std::string provider, std::string app, Report, Ready, bool preview_worker = false);
  ~TransferWorker();
  void Begin(TransferRequest);
  void Cancel(std::uint64_t next_epoch);
  void Stop();
 private:
  void Run();
  void Transfer(TransferRequest);
  fs::path profile_, local_source_;
  std::string provider_, app_;
  fs::path identity_;
  Report report_;
  Ready ready_;
  std::atomic<bool> stop_{false};
  std::atomic<std::uint64_t> wanted_{0};
  std::mutex mutex_;
  std::condition_variable condition_;
  std::unique_ptr<TransferRequest> pending_;
  std::thread thread_;
};
bool AppendDurable(fs::path const&, Bytes const&);
}
