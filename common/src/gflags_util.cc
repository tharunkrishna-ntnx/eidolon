#include "eidolon/gflags_util.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <fstream>
#include <sstream>
#include <vector>

namespace eidolon {
namespace gflags_util {

static void TrimInPlace(std::string& s) {
  auto start = s.find_first_not_of(" \t\r\n");
  auto end = s.find_last_not_of(" \t\r\n");
  if (start == std::string::npos) {
    s.clear();
    return;
  }
  s = s.substr(start, end - start + 1);
}

std::unordered_map<std::string, std::string> ParseFile(
    const std::string& path) {
  std::unordered_map<std::string, std::string> flags;
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return flags;
  }

  std::string line;
  while (std::getline(ifs, line)) {
    TrimInPlace(line);
    if (line.empty() || line[0] == '#') continue;

    // Strip leading dashes.
    size_t pos = 0;
    while (pos < line.size() && line[pos] == '-') ++pos;
    line = line.substr(pos);

    auto eq = line.find('=');
    if (eq == std::string::npos) continue;

    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    TrimInPlace(key);
    TrimInPlace(val);
    if (!key.empty()) {
      flags[key] = val;
    }
  }
  return flags;
}

int LoadFromFile(const std::string& path) {
  auto flags = ParseFile(path);
  int applied = 0;
  for (auto& [key, val] : flags) {
    std::string err;
    if (gflags::SetCommandLineOption(key.c_str(), val.c_str()).empty()) {
      LOG(WARNING) << "Unknown gflag '" << key << "' in " << path;
    } else {
      ++applied;
    }
  }
  LOG(INFO) << "Loaded " << applied << " gflags from " << path;
  return applied;
}

void SaveToFile(const std::string& path) {
  std::ofstream ofs(path, std::ios::trunc);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Cannot write gflags file: " << path;
    return;
  }

  std::vector<gflags::CommandLineFlagInfo> all_flags;
  gflags::GetAllFlags(&all_flags);
  for (auto& fi : all_flags) {
    if (fi.is_default) continue;
    ofs << "--" << fi.name << "=" << fi.current_value << "\n";
  }
}

std::string GflagsPath(const std::string& base_dir,
                       const std::string& service) {
  return base_dir + "/" + service + ".gflags";
}

}  // namespace gflags_util
}  // namespace eidolon
