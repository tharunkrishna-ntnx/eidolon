#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eidolon {
namespace mnemosyne {

// In-memory key-value store with a configurable memory cap.
class Store {
 public:
  explicit Store(size_t memory_limit_bytes);

  bool Put(const std::string& key, const std::string& value);
  bool Get(const std::string& key, std::string* value) const;
  bool Delete(const std::string& key);
  std::vector<std::pair<std::string, std::string>> Scan(
      const std::string& prefix) const;

  size_t MemoryUsed() const;
  size_t MemoryLimit() const { return memory_limit_; }
  size_t NumKeys() const;

  // Persist the store to a directory (one file per partition shard).
  void SaveToDisk(const std::string& dir) const;
  void LoadFromDisk(const std::string& dir);

 private:
  size_t memory_limit_;
  mutable std::mutex mu_;
  std::unordered_map<std::string, std::string> data_;
  size_t current_bytes_ = 0;
};

}  // namespace mnemosyne
}  // namespace eidolon
