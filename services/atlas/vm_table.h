#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace eidolon {
namespace atlas {

struct VmDescriptor {
  uint64_t vm_id;
  uint32_t vcpus;
  uint32_t memory_mb;
  uint32_t disk_gb;
  int32_t  placed_node;   // -1 = unplaced
  uint8_t  state;         // 0=pending, 1=running, 2=stopped
  char     padding[64];   // cache-line filler for realistic struct size
};

// In-memory table of mock VM descriptors.  Supports random-access lookups
// that touch scattered memory when the table is large enough to exceed
// the CPU cache, causing measurable cache misses under profiling.
class VmTable {
 public:
  explicit VmTable(size_t size_mb);

  VmDescriptor& At(size_t index);
  const VmDescriptor& At(size_t index) const;
  size_t Size() const { return vms_.size(); }

  // Random-access lookup: hash the key, read scattered entries.
  // Returns the number of entries touched.
  size_t RandomLookup(uint64_t key, size_t count);

  size_t UnplacedCount() const;

 private:
  std::vector<VmDescriptor> vms_;
};

}  // namespace atlas
}  // namespace eidolon
