#pragma once

#include <string>
#include <unordered_map>

namespace eidolon {
namespace gflags_util {

// Load gflag values from a .gflags file.
// File format: one flag per line, e.g.
//   --memory_limit_mb=256
//   --replication_factor=2
// Lines starting with '#' and blank lines are ignored.
// Returns the number of flags successfully applied.
int LoadFromFile(const std::string& path);

// Parse all flags from |path| into a key-value map without applying them.
std::unordered_map<std::string, std::string> ParseFile(const std::string& path);

// Write the current value of all registered gflags back to |path| in the
// canonical --flag=value format.
void SaveToFile(const std::string& path);

// Return the resolved path for a service's gflags file.
//   base_dir / <service>.gflags
std::string GflagsPath(const std::string& base_dir,
                       const std::string& service);

}  // namespace gflags_util
}  // namespace eidolon
