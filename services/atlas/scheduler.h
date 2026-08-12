#pragma once

#include "vm_table.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eidolon {
namespace atlas {

struct NodeResources {
  std::string ip;
  uint32_t total_vcpus = 16;
  uint32_t total_memory_mb = 32768;
  uint32_t used_vcpus = 0;
  uint32_t used_memory_mb = 0;
};

// Placement scheduler that scores node fitness for a VM request.
// The search depth controls how many candidate placements are evaluated
// per VM -- higher depth = more CPU work per scheduling cycle.
class Scheduler {
 public:
  explicit Scheduler(int depth);

  // Run one placement cycle over the VM table.
  // Returns the number of VMs successfully placed.
  size_t RunCycle(VmTable& table, const std::vector<NodeResources>& nodes);

 private:
  // Score a single placement candidate.  The function deliberately does
  // enough floating-point math to be visible in a flamegraph.
  double ScorePlacement(const VmDescriptor& vm,
                        const NodeResources& node,
                        int iteration);

  int depth_;
};

}  // namespace atlas
}  // namespace eidolon
