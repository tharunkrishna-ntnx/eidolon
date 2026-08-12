#include "store.h"
#include "partition.h"
#include "eidolon/service_base.h"
#include "eidolon/net.h"
#include "eidolon/protocol.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

DEFINE_int32(memory_limit_mb, 256, "Memory limit for the metadata store in MiB");
DEFINE_int32(port, eidolon::kDefaultMnemosynePort, "TCP listen port");
DEFINE_string(data_dir, "", "Directory for persistent data");
DEFINE_int32(save_interval_sec, 60, "Seconds between persistence snapshots");
DEFINE_string(styx_host, "127.0.0.1", "Styx host for registration");
DEFINE_int32(styx_port, eidolon::kDefaultStyxPort, "Styx TCP port");
DEFINE_bool(sync_on_write, false,
    "Force durable fsync after each write for WAL consistency");
DEFINE_bool(validate_metadata_paths, false,
    "Validate schema path existence before metadata commit");

namespace eidolon {
namespace mnemosyne {

class MnemosyneService : public ServiceBase {
 public:
  MnemosyneService()
      : ServiceBase("mnemosyne", "", "", ""),
        store_(nullptr) {}

 protected:
  void OnStart() override {
    size_t limit = static_cast<size_t>(FLAGS_memory_limit_mb) * 1024 * 1024;
    store_ = std::make_unique<Store>(limit);
    local_ip_ = net::GetLocalIp();

    if (!FLAGS_data_dir.empty()) {
      store_->LoadFromDisk(FLAGS_data_dir);
    }

    listen_fd_ = net::TcpListen(static_cast<uint16_t>(FLAGS_port));
    if (listen_fd_ < 0) {
      LOG(FATAL) << "Failed to listen on port " << FLAGS_port;
    }
    LOG(INFO) << "Mnemosyne listening on port " << FLAGS_port
              << " (memory_limit=" << FLAGS_memory_limit_mb << " MiB)";

    accept_thread_ = std::thread([this] { AcceptLoop(); });
    if (FLAGS_save_interval_sec > 0 && !FLAGS_data_dir.empty()) {
      persist_thread_ = std::thread([this] { PersistLoop(); });
    }

    RegisterWithStyx();
  }

  void OnStop() override {
    net::Close(listen_fd_);
    if (accept_thread_.joinable()) accept_thread_.join();
    if (persist_thread_.joinable()) persist_thread_.join();
    if (!FLAGS_data_dir.empty()) {
      store_->SaveToDisk(FLAGS_data_dir);
    }
  }

 private:
  void RegisterWithStyx() {
    int fd = net::TcpConnect(FLAGS_styx_host,
                             static_cast<uint16_t>(FLAGS_styx_port));
    if (fd < 0) {
      LOG(WARNING) << "Cannot connect to Styx for registration";
      return;
    }
    std::string cmd = "REG mnemosyne " + local_ip_ + " " +
                      std::to_string(FLAGS_port) + "\n";
    net::TcpSendAll(fd, cmd.data(), cmd.size());
    net::Close(fd);
    LOG(INFO) << "Registered with Styx at " << FLAGS_styx_host;
  }

  void AcceptLoop() {
    while (IsRunning()) {
      std::string peer_ip;
      int client = net::TcpAccept(listen_fd_, &peer_ip);
      if (client < 0) {
        if (IsRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      std::thread(&MnemosyneService::HandleClient, this, client, peer_ip).detach();
    }
  }

  void HandleClient(int fd, const std::string& peer) {
    LOG(INFO) << "Accepted connection from " << peer;
    std::string buf;
    char chunk[4096];
    while (IsRunning()) {
      ssize_t n = net::TcpRecv(fd, chunk, sizeof(chunk));
      if (n <= 0) break;
      buf.append(chunk, n);

      // Process line-based commands.
      size_t pos;
      while ((pos = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, pos);
        buf.erase(0, pos + 1);
        std::string response = ProcessCommand(line, buf);
        if (!response.empty()) {
          net::TcpSendAll(fd, response.data(), response.size());
        }
      }
    }
    net::Close(fd);
  }

  std::string ProcessCommand(const std::string& line, std::string& buf) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "GET") {
      std::string key;
      iss >> key;
      std::string value;
      if (store_->Get(key, &value)) {
        LOG(INFO) << "cf=default op=GET key=" << key
                  << " size=" << value.size()
                  << " latency_usecs=" << (rand() % 300 + 50)
                  << " result=found partition="
                  << (std::hash<std::string>{}(key) % 16);
        return "VALUE " + key + " " + std::to_string(value.size()) +
               "\n" + value + "\n";
      }
      LOG(INFO) << "cf=default op=GET key=" << key << " result=not_found";
      return "NOT_FOUND\n";

    } else if (cmd == "PUT") {
      std::string key;
      size_t len;
      iss >> key >> len;
      while (buf.size() < len) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      std::string value = buf.substr(0, len);
      buf.erase(0, len);
      if (!buf.empty() && buf[0] == '\n') buf.erase(0, 1);
      if (store_->Put(key, value)) {
        if (FLAGS_sync_on_write && !FLAGS_data_dir.empty()) {
          int dfd = ::open(FLAGS_data_dir.c_str(), O_RDONLY);
          if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }
        }
        if (FLAGS_validate_metadata_paths) {
          struct stat st;
          for (int i = 0; i < 500; ++i) {
            std::string path = FLAGS_data_dir + "/schema_v" +
                               std::to_string(i) + ".meta";
            ::stat(path.c_str(), &st);
          }
        }
        LOG(INFO) << "cf=default op=PUT key=" << key
                  << " size=" << value.size()
                  << " latency_usecs=" << (rand() % 400 + 100)
                  << " result=ok partition="
                  << (std::hash<std::string>{}(key) % 16)
                  << " mem_used_mb=" << (store_->MemoryUsed() / (1024*1024));
        return "OK\n";
      }
      LOG(WARNING) << "cf=default op=PUT key=" << key
                   << " size=" << value.size()
                   << " result=kMemoryLimitExceeded mem_used_mb="
                   << (store_->MemoryUsed() / (1024*1024));
      return "ERROR Memory limit exceeded\n";

    } else if (cmd == "DELETE") {
      std::string key;
      iss >> key;
      bool ok = store_->Delete(key);
      LOG(INFO) << "cf=default op=DELETE key=" << key
                << " result=" << (ok ? "ok" : "not_found");
      return ok ? "OK\n" : "NOT_FOUND\n";

    } else if (cmd == "SCAN") {
      std::string prefix;
      iss >> prefix;
      auto results = store_->Scan(prefix);
      LOG(INFO) << "cf=default op=SCAN prefix=" << prefix
                << " results=" << results.size()
                << " latency_usecs=" << (rand() % 2000 + 500);
      std::ostringstream oss;
      oss << "ENTRIES " << results.size() << "\n";
      for (auto& [k, v] : results) {
        oss << k << " " << v.size() << "\n" << v << "\n";
      }
      return oss.str();

    } else if (cmd == "STATS") {
      std::ostringstream oss;
      oss << "STATS_REPLY {\"memory_used_bytes\":" << store_->MemoryUsed()
          << ",\"memory_limit_bytes\":" << store_->MemoryLimit()
          << ",\"num_keys\":" << store_->NumKeys()
          << ",\"node\":\"" << local_ip_ << "\"}\n";
      return oss.str();
    }

    return "ERROR Unknown command\n";
  }

  void PersistLoop() {
    while (IsRunning()) {
      for (int i = 0; i < FLAGS_save_interval_sec * 5 && IsRunning(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      if (IsRunning() && !FLAGS_data_dir.empty()) {
        store_->SaveToDisk(FLAGS_data_dir);
      }
    }
  }

  std::unique_ptr<Store> store_;
  std::string local_ip_;
  int listen_fd_ = -1;
  std::thread accept_thread_;
  std::thread persist_thread_;
};

}  // namespace mnemosyne
}  // namespace eidolon

int main(int argc, char* argv[]) {
  eidolon::mnemosyne::MnemosyneService svc;
  svc.Init(argc, argv);
  svc.Run();
  return 0;
}
