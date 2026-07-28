#include "partition.h"

#include <algorithm>
#include <functional>

namespace eidolon {
namespace mnemosyne {

void PartitionMap::SetNodes(const std::vector<std::string>& node_ips) {
  nodes_ = node_ips;
  std::sort(nodes_.begin(), nodes_.end());
}

uint32_t PartitionMap::Hash(const std::string& s) const {
  // Simple FNV-1a 32-bit hash.
  uint32_t h = 2166136261u;
  for (char c : s) {
    h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
    h *= 16777619u;
  }
  return h;
}

std::string PartitionMap::OwnerOf(const std::string& key) const {
  if (nodes_.empty()) return "";
  uint32_t h = Hash(key);
  return nodes_[h % nodes_.size()];
}

bool PartitionMap::IsLocal(const std::string& key,
                           const std::string& local_ip) const {
  return OwnerOf(key) == local_ip;
}

}  // namespace mnemosyne
}  // namespace eidolon
