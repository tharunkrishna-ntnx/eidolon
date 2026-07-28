#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eidolon {
namespace mnemosyne {

// Determines which node owns a given key using consistent hashing.
class PartitionMap {
 public:
  // Set the list of node IPs participating in the ring.
  void SetNodes(const std::vector<std::string>& node_ips);

  // Return the IP of the node responsible for |key|.
  std::string OwnerOf(const std::string& key) const;

  // Return true if |local_ip| is responsible for |key|.
  bool IsLocal(const std::string& key, const std::string& local_ip) const;

  size_t NumNodes() const { return nodes_.size(); }

 private:
  std::vector<std::string> nodes_;  // sorted
  uint32_t Hash(const std::string& s) const;
};

}  // namespace mnemosyne
}  // namespace eidolon
