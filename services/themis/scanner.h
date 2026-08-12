#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eidolon {
namespace themis {

struct NodeStats {
  std::string ip;
  uint64_t num_blocks = 0;
  uint64_t total_bytes = 0;
};

// Scans Mnemosyne and Hermes instances across the cluster to build a picture
// of data distribution.
class Scanner {
 public:
  Scanner(const std::string& styx_host, uint16_t styx_port);

  // Run a full scan.  Returns true if the scan completed successfully.
  bool RunScan();

  const std::vector<NodeStats>& Results() const { return results_; }
  bool IsImbalanced(double threshold_pct = 20.0) const;

  // Identify blocks that have fewer replicas than desired.
  struct UnderReplicatedBlock {
    std::string block_id;
    std::vector<std::string> current_nodes;
    int desired_replicas;
  };
  const std::vector<UnderReplicatedBlock>& UnderReplicated() const {
    return under_replicated_;
  }

 private:
  std::string styx_host_;
  uint16_t styx_port_;
  std::vector<NodeStats> results_;
  std::vector<UnderReplicatedBlock> under_replicated_;
};

}  // namespace themis
}  // namespace eidolon
