#pragma once

#include <string>

namespace eidolon {
namespace logging {

// Initialise glog-based logging for a service.
//   component  - service name (e.g. "hermes"), used as log filename prefix
//   log_dir    - directory for log files (e.g. ~/data/logs)
//
// Installs a budget enforcer that automatically prunes the oldest log
// files for this component when total size exceeds --log_budget_mb
// (default 4 GiB).  The check runs every 5000 log messages, so it's
// lightweight and gradual.
//
// Creates glog-style severity symlinks:
//   <component>.INFO, <component>.WARNING, etc.
void Init(const std::string& component, const std::string& log_dir);

// Flush all pending log output.
void Flush();

// Shut down logging cleanly.
void Shutdown();

}  // namespace logging
}  // namespace eidolon

// Convenience: services just #include this header and use LOG(INFO), etc.
// from glog which is transitively included.
#include <glog/logging.h>
