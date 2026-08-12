#include "vm_table.h"

#include <cstring>

namespace eidolon {
namespace atlas {

VmTable::VmTable(size_t size_mb) {
  size_t num = (size_mb * 1024ULL * 1024ULL) / sizeof(VmDescriptor);
  if (num < 16) num = 16;
  vms_.resize(num);

  for (size_t i = 0; i < num; ++i) {
    auto& vm = vms_[i];
    vm.vm_id = i;
    vm.vcpus = 1 + (i % 8);
    vm.memory_mb = 512 * (1 + (i % 16));
    vm.disk_gb = 10 + (i % 100);
    vm.placed_node = (i % 5 == 0) ? -1 : static_cast<int32_t>(i % 3);
    vm.state = (vm.placed_node >= 0) ? 1 : 0;
    std::memset(vm.padding, 0, sizeof(vm.padding));
  }
}

VmDescriptor& VmTable::At(size_t index) {
  return vms_[index % vms_.size()];
}

const VmDescriptor& VmTable::At(size_t index) const {
  return vms_[index % vms_.size()];
}

size_t VmTable::RandomLookup(uint64_t key, size_t count) {
  volatile uint64_t sink = 0;
  size_t n = vms_.size();
  uint64_t h = key;
  for (size_t i = 0; i < count; ++i) {
    h = h * 6364136223846793005ULL + 1442695040888963407ULL;
    size_t idx = h % n;
    sink += vms_[idx].vm_id + vms_[idx].memory_mb;
  }
  return count;
}

size_t VmTable::UnplacedCount() const {
  size_t count = 0;
  for (auto& vm : vms_) {
    if (vm.placed_node < 0) ++count;
  }
  return count;
}

}  // namespace atlas
}  // namespace eidolon
