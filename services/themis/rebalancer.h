#pragma once

#include "scanner.h"
#include <string>
#include <vector>

namespace eidolon {
namespace themis {

// Orchestrates data movement between Hermes instances to restore balance.
class Rebalancer {
 public:
  struct MoveOp {
    std::string block_id;
    std::string from_ip;
    uint16_t from_port;
    std::string to_ip;
    uint16_t to_port;
  };

  // Given scan results, compute a set of moves to even out data distribution.
  std::vector<MoveOp> Plan(const std::vector<NodeStats>& stats) const;

  // Execute a single move operation (tell source Hermes to replicate, then
  // delete from source if it's over-loaded).
  bool Execute(const MoveOp& op) const;
};

}  // namespace themis
}  // namespace eidolon
