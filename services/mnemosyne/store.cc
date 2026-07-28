#include "store.h"

#include <glog/logging.h>

#include <filesystem>
#include <fstream>

namespace eidolon {
namespace mnemosyne {

Store::Store(size_t memory_limit_bytes)
    : memory_limit_(memory_limit_bytes) {}

bool Store::Put(const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> lk(mu_);
  size_t entry_size = key.size() + value.size();

  auto it = data_.find(key);
  if (it != data_.end()) {
    // Update: account for size difference.
    size_t old_size = it->first.size() + it->second.size();
    size_t new_total = current_bytes_ - old_size + entry_size;
    if (new_total > memory_limit_) {
      LOG(ERROR) << "Memory limit exceeded: used=" << new_total
                 << " limit=" << memory_limit_
                 << " (PUT key=" << key << ")";
      return false;
    }
    current_bytes_ = new_total;
    it->second = value;
  } else {
    if (current_bytes_ + entry_size > memory_limit_) {
      LOG(ERROR) << "Memory limit exceeded: used="
                 << (current_bytes_ + entry_size)
                 << " limit=" << memory_limit_
                 << " (PUT key=" << key << ")";
      return false;
    }
    current_bytes_ += entry_size;
    data_[key] = value;
  }
  return true;
}

bool Store::Get(const std::string& key, std::string* value) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = data_.find(key);
  if (it == data_.end()) return false;
  if (value) *value = it->second;
  return true;
}

bool Store::Delete(const std::string& key) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = data_.find(key);
  if (it == data_.end()) return false;
  current_bytes_ -= (it->first.size() + it->second.size());
  data_.erase(it);
  return true;
}

std::vector<std::pair<std::string, std::string>> Store::Scan(
    const std::string& prefix) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<std::pair<std::string, std::string>> results;
  for (auto& [k, v] : data_) {
    if (k.compare(0, prefix.size(), prefix) == 0) {
      results.emplace_back(k, v);
    }
  }
  return results;
}

size_t Store::MemoryUsed() const {
  std::lock_guard<std::mutex> lk(mu_);
  return current_bytes_;
}

size_t Store::NumKeys() const {
  std::lock_guard<std::mutex> lk(mu_);
  return data_.size();
}

void Store::SaveToDisk(const std::string& dir) const {
  namespace fs = std::filesystem;
  fs::create_directories(dir);
  std::lock_guard<std::mutex> lk(mu_);
  std::ofstream ofs(dir + "/mnemosyne.dat", std::ios::trunc | std::ios::binary);
  for (auto& [k, v] : data_) {
    uint32_t klen = static_cast<uint32_t>(k.size());
    uint32_t vlen = static_cast<uint32_t>(v.size());
    ofs.write(reinterpret_cast<const char*>(&klen), 4);
    ofs.write(k.data(), klen);
    ofs.write(reinterpret_cast<const char*>(&vlen), 4);
    ofs.write(v.data(), vlen);
  }
  LOG(INFO) << "Persisted " << data_.size() << " keys to " << dir;
}

void Store::LoadFromDisk(const std::string& dir) {
  std::string path = dir + "/mnemosyne.dat";
  if (!std::filesystem::exists(path)) return;

  std::ifstream ifs(path, std::ios::binary);
  std::lock_guard<std::mutex> lk(mu_);
  data_.clear();
  current_bytes_ = 0;

  while (ifs.good() && ifs.peek() != EOF) {
    uint32_t klen = 0, vlen = 0;
    ifs.read(reinterpret_cast<char*>(&klen), 4);
    if (!ifs.good()) break;
    std::string key(klen, '\0');
    ifs.read(&key[0], klen);
    ifs.read(reinterpret_cast<char*>(&vlen), 4);
    if (!ifs.good()) break;
    std::string val(vlen, '\0');
    ifs.read(&val[0], vlen);
    current_bytes_ += klen + vlen;
    data_[std::move(key)] = std::move(val);
  }
  LOG(INFO) << "Loaded " << data_.size() << " keys from " << path
            << " (" << current_bytes_ << " bytes)";
}

}  // namespace mnemosyne
}  // namespace eidolon
