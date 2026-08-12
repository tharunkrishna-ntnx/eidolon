#include "eidolon/logging.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <regex>
#include <vector>

DEFINE_int64(log_budget_mb, 4096,
             "Max total log bytes per service before oldest files are pruned");

namespace eidolon {
namespace logging {

namespace {

struct LogBudgetEnforcer : public google::LogSink {
  std::string log_dir;
  std::string component;
  size_t budget_bytes = 0;
  size_t max_log_size_bytes = 0;

  std::mutex mu;
  uint64_t msg_count = 0;
  static constexpr uint64_t kCheckInterval = 5000;

  void send(google::LogSeverity, const char*, const char*,
            int, const google::LogMessageTime&,
            const char*, size_t) override {
    if (++msg_count % kCheckInterval != 0) return;
    EnforceBudget();
  }

  void EnforceBudget() {
    namespace fs = std::filesystem;
    std::lock_guard<std::mutex> lk(mu);
    if (!fs::is_directory(log_dir)) return;

    std::regex pat(component +
                   R"(\..+\.log\.(INFO|WARNING|ERROR|FATAL)\.\d{8}-\d{6}\.\d+)");

    struct LogFile {
      fs::path path;
      uintmax_t size;
      fs::file_time_type mtime;
    };
    std::vector<LogFile> files;
    uintmax_t total = 0;

    for (auto& entry : fs::directory_iterator(log_dir)) {
      if (!entry.is_regular_file()) continue;
      if (!std::regex_match(entry.path().filename().string(), pat)) continue;
      auto sz = entry.file_size();
      total += sz;
      files.push_back({entry.path(), sz, entry.last_write_time()});
    }

    if (total <= budget_bytes) return;

    // Sort oldest-first so we delete from the front.
    std::sort(files.begin(), files.end(),
              [](const LogFile& a, const LogFile& b) {
                return a.mtime < b.mtime;
              });

    // Delete oldest until we have room for one new max-size log file.
    uintmax_t target = budget_bytes - std::min(budget_bytes, max_log_size_bytes);
    for (auto& f : files) {
      if (total <= target) break;
      // Never delete the newest file of any severity (it's the active log).
      std::error_code ec;
      fs::remove(f.path, ec);
      if (!ec) total -= f.size;
    }
  }
};

LogBudgetEnforcer* g_enforcer = nullptr;

}  // namespace


void Init(const std::string& component, const std::string& log_dir) {
  namespace fs = std::filesystem;
  fs::create_directories(log_dir);

  size_t max_log_mb = 50;

  FLAGS_log_dir = log_dir;
  FLAGS_alsologtostderr = false;
  FLAGS_logbufsecs = 0;
  FLAGS_max_log_size = static_cast<int32_t>(max_log_mb);
  FLAGS_stop_logging_if_full_disk = true;

  google::InitGoogleLogging(component.c_str());

  google::SetLogSymlink(google::INFO,    component.c_str());
  google::SetLogSymlink(google::WARNING, component.c_str());
  google::SetLogSymlink(google::ERROR,   component.c_str());
  google::SetLogSymlink(google::FATAL,   component.c_str());

  // Install the budget enforcer as a glog sink so it runs inline
  // with logging -- every kCheckInterval messages it scans the log
  // directory and prunes oldest files when total size exceeds budget.
  auto* e = new LogBudgetEnforcer();
  e->log_dir = log_dir;
  e->component = component;
  e->budget_bytes = static_cast<size_t>(FLAGS_log_budget_mb) * 1024ULL * 1024ULL;
  e->max_log_size_bytes = max_log_mb * 1024ULL * 1024ULL;
  g_enforcer = e;
  google::AddLogSink(e);

  // Run one immediate pass at startup to clean up leftovers.
  e->EnforceBudget();

  LOG(INFO) << component << " logging initialised, log_dir=" << log_dir
            << " budget_mb=" << FLAGS_log_budget_mb;
}

void Flush() {
  google::FlushLogFiles(google::INFO);
}

void Shutdown() {
  Flush();
  if (g_enforcer) {
    google::RemoveLogSink(g_enforcer);
    delete g_enforcer;
    g_enforcer = nullptr;
  }
  google::ShutdownGoogleLogging();
}

}  // namespace logging
}  // namespace eidolon
