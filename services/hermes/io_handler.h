#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <atomic>

namespace eidolon {
namespace hermes {

struct BlockMeta {
  std::string block_id;
  size_t size = 0;
  std::string checksum;
};

// Manages block storage on the local filesystem.
class IoHandler {
 public:
  explicit IoHandler(const std::string& blocks_dir);

  bool WriteBlock(const std::string& block_id, const std::string& data);
  bool ReadBlock(const std::string& block_id, std::string* data);
  bool DeleteBlock(const std::string& block_id);
  bool HasBlock(const std::string& block_id) const;

  size_t NumBlocks() const;
  size_t TotalBytes() const;

  uint64_t ReadsCompleted() const { return reads_.load(); }
  uint64_t WritesCompleted() const { return writes_.load(); }

 private:
  std::string BlockPath(const std::string& block_id) const;

  std::string blocks_dir_;
  mutable std::mutex mu_;
  std::unordered_map<std::string, BlockMeta> index_;
  std::atomic<uint64_t> reads_{0};
  std::atomic<uint64_t> writes_{0};
  size_t total_bytes_ = 0;
};

}  // namespace hermes
}  // namespace eidolon
