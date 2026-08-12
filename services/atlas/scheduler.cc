#include "scheduler.h"

#include <cmath>
#include <limits>

namespace eidolon {
namespace atlas {

Scheduler::Scheduler(int depth) : depth_(depth) {}

double Scheduler::ScorePlacement(const VmDescriptor& vm,
                                 const NodeResources& node,
                                 int iteration) {
  uint32_t avail_cpu = node.total_vcpus - node.used_vcpus;
  uint32_t avail_mem = node.total_memory_mb - node.used_memory_mb;

  if (vm.vcpus > avail_cpu || vm.memory_mb > avail_mem) {
    return -1.0;
  }

  // Deliberate floating-point work that scales with depth.
  // At high depths, this dominates the flamegraph.
  double cpu_fit = static_cast<double>(avail_cpu - vm.vcpus) / node.total_vcpus;
  double mem_fit = static_cast<double>(avail_mem - vm.memory_mb) / node.total_memory_mb;
  double score = 0.0;

  for (int i = 0; i < depth_; ++i) {
    double weight = 1.0 / (1.0 + i + iteration);
    score += std::sqrt(cpu_fit * cpu_fit + mem_fit * mem_fit) * weight;
    score += std::sin(cpu_fit * (i + 1)) * 0.01;
    score += std::cos(mem_fit * (i + 1)) * 0.01;
  }
  return score;
}

size_t Scheduler::RunCycle(VmTable& table,
                           const std::vector<NodeResources>& nodes) {
  if (nodes.empty() || depth_ <= 0) return 0;

  size_t placed = 0;
  size_t n = table.Size();

  for (size_t i = 0; i < n; ++i) {
    auto& vm = table.At(i);
    if (vm.placed_node >= 0) continue;

    double best_score = -1.0;
    int best_node = -1;

    for (size_t ni = 0; ni < nodes.size(); ++ni) {
      double s = ScorePlacement(vm, nodes[ni], static_cast<int>(i));
      if (s > best_score) {
        best_score = s;
        best_node = static_cast<int>(ni);
      }
    }

    if (best_node >= 0) {
      vm.placed_node = best_node;
      vm.state = 1;
      ++placed;
      if (placed >= 10) break;
    }
  }
  return placed;
}

}  // namespace atlas
}  // namespace eidolon
