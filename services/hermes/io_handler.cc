#include "io_handler.h"

#include <glog/logging.h>

#include <filesystem>
#include <fstream>
#include <functional>

namespace eidolon {
namespace hermes {

IoHandler::IoHandler(const std::string& blocks_dir)
    : blocks_dir_(blocks_dir) {
  std::filesystem::create_directories(blocks_dir_);

  // Rebuild index from existing files on startup.
  for (auto& entry : std::filesystem::directory_iterator(blocks_dir_)) {
    if (!entry.is_regular_file()) continue;
    BlockMeta meta;
    meta.block_id = entry.path().filename().string();
    meta.size = entry.file_size();
    total_bytes_ += meta.size;
    index_[meta.block_id] = std::move(meta);
  }
  LOG(INFO) << "IoHandler loaded " << index_.size()
            << " existing blocks (" << total_bytes_ << " bytes)";
}

std::string IoHandler::BlockPath(const std::string& block_id) const {
  return blocks_dir_ + "/" + block_id;
}

bool IoHandler::WriteBlock(const std::string& block_id,
                           const std::string& data) {
  std::string path = BlockPath(block_id);
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    LOG(ERROR) << "Cannot open block file for write: " << path;
    return false;
  }
  ofs.write(data.data(), data.size());
  ofs.close();

  std::lock_guard<std::mutex> lk(mu_);
  auto it = index_.find(block_id);
  if (it != index_.end()) {
    total_bytes_ -= it->second.size;
  }
  BlockMeta meta;
  meta.block_id = block_id;
  meta.size = data.size();
  total_bytes_ += data.size();
  index_[block_id] = std::move(meta);
  writes_++;
  return true;
}

bool IoHandler::ReadBlock(const std::string& block_id, std::string* data) {
  std::string path = BlockPath(block_id);
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) return false;

  ifs.seekg(0, std::ios::end);
  size_t sz = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  data->resize(sz);
  ifs.read(&(*data)[0], sz);
  reads_++;
  return true;
}

bool IoHandler::DeleteBlock(const std::string& block_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = index_.find(block_id);
  if (it == index_.end()) return false;
  total_bytes_ -= it->second.size;
  index_.erase(it);
  std::filesystem::remove(BlockPath(block_id));
  return true;
}

bool IoHandler::HasBlock(const std::string& block_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  return index_.count(block_id) > 0;
}

size_t IoHandler::NumBlocks() const {
  std::lock_guard<std::mutex> lk(mu_);
  return index_.size();
}

size_t IoHandler::TotalBytes() const {
  std::lock_guard<std::mutex> lk(mu_);
  return total_bytes_;
}

}  // namespace hermes
}  // namespace eidolon
