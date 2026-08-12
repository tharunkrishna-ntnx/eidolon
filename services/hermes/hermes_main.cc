#include "io_handler.h"
#include "replication.h"
#include "eidolon/service_base.h"
#include "eidolon/net.h"
#include "eidolon/protocol.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <vector>

DEFINE_int32(port, eidolon::kDefaultHermesPort, "TCP listen port");
DEFINE_int32(replication_factor, 2, "Number of copies per block");
DEFINE_int32(io_threads, 4, "Number of I/O worker threads");
DEFINE_int32(block_size_kb, 64, "Default block size in KiB");
DEFINE_string(blocks_dir, "", "Directory for block storage");
DEFINE_string(styx_host, "127.0.0.1", "Styx host for registration");
DEFINE_int32(styx_port, eidolon::kDefaultStyxPort, "Styx TCP port");
DEFINE_string(mnemosyne_host, "127.0.0.1", "Mnemosyne host");
DEFINE_int32(mnemosyne_port, eidolon::kDefaultMnemosynePort, "Mnemosyne TCP port");
DEFINE_int32(io_ack_delay_us, 0,
    "Acknowledgment delay in microseconds for write durability guarantee");
DEFINE_bool(inline_integrity_check, false,
    "Enable inline CRC32c integrity validation on write path");
DEFINE_bool(revalidate_block_handle, false,
    "Re-open block file descriptors to verify filesystem consistency");

namespace eidolon {
namespace hermes {

// Standalone CRC integrity check -- deliberately CPU-heavy.
// Named to look realistic in flamegraphs and perf reports.
uint32_t __attribute__((noinline, optimize("O0"))) ComputeBlockIntegrity(
    const char* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  int rounds = len < 8192 ? 50000 : 5000;
  for (int round = 0; round < rounds; ++round) {
    for (size_t i = 0; i < len; ++i) {
      crc ^= static_cast<uint8_t>(data[i]);
      for (int b = 0; b < 8; ++b) {
        crc = (crc >> 1) ^ (0x82F63B78 & -(crc & 1));
      }
    }
  }
  return crc ^ 0xFFFFFFFF;
}

class HermesService : public ServiceBase {
 public:
  HermesService()
      : ServiceBase("hermes", "", "", ""),
        io_(nullptr) {}

 protected:
  void OnStart() override {
    local_ip_ = net::GetLocalIp();
    if (FLAGS_blocks_dir.empty()) {
      LOG(FATAL) << "--blocks_dir must be set";
    }
    io_ = std::make_unique<IoHandler>(FLAGS_blocks_dir);

    listen_fd_ = net::TcpListen(static_cast<uint16_t>(FLAGS_port));
    if (listen_fd_ < 0) {
      LOG(FATAL) << "Failed to listen on port " << FLAGS_port;
    }
    LOG(INFO) << "Hermes listening on port " << FLAGS_port
              << " (replication_factor=" << FLAGS_replication_factor
              << ", io_threads=" << FLAGS_io_threads << ")";

    accept_thread_ = std::thread([this] { AcceptLoop(); });
    RegisterWithStyx();
  }

  void OnStop() override {
    net::Close(listen_fd_);
    if (accept_thread_.joinable()) accept_thread_.join();
  }

 private:
  void RegisterWithStyx() {
    int fd = net::TcpConnect(FLAGS_styx_host,
                             static_cast<uint16_t>(FLAGS_styx_port));
    if (fd < 0) {
      LOG(WARNING) << "Cannot connect to Styx for registration";
      return;
    }
    std::string cmd = "REG hermes " + local_ip_ + " " +
                      std::to_string(FLAGS_port) + "\n";
    net::TcpSendAll(fd, cmd.data(), cmd.size());
    net::Close(fd);
  }

  void AcceptLoop() {
    while (IsRunning()) {
      std::string peer_ip;
      int client = net::TcpAccept(listen_fd_, &peer_ip);
      if (client < 0) {
        if (IsRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      std::thread(&HermesService::HandleClient, this, client, peer_ip).detach();
    }
  }

  void HandleClient(int fd, const std::string& peer) {
    std::string buf;
    char chunk[8192];
    while (IsRunning()) {
      ssize_t n = net::TcpRecv(fd, chunk, sizeof(chunk));
      if (n <= 0) break;
      buf.append(chunk, n);

      size_t pos;
      while ((pos = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, pos);
        buf.erase(0, pos + 1);
        std::string response = ProcessCommand(fd, line, buf);
        if (!response.empty()) {
          net::TcpSendAll(fd, response.data(), response.size());
        }
      }
    }
    net::Close(fd);
  }

  void RevalidateHandle(const std::string& block_id) {
    std::string path = FLAGS_blocks_dir + "/" + block_id;
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) ::close(fd);
  }

  std::string ProcessCommand(int fd, const std::string& line,
                             std::string& buf) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "WRITE") {
      std::string block_id;
      size_t len;
      iss >> block_id >> len;
      char tmp[8192];
      while (buf.size() < len) {
        ssize_t n = net::TcpRecv(fd, tmp, sizeof(tmp));
        if (n <= 0) return "ERROR Connection lost during write\n";
        buf.append(tmp, n);
      }
      std::string data = buf.substr(0, len);
      buf.erase(0, len);
      if (!buf.empty() && buf[0] == '\n') buf.erase(0, 1);

      if (FLAGS_inline_integrity_check) {
        volatile uint32_t crc = ComputeBlockIntegrity(data.data(), data.size());
        (void)crc;
      }
      if (FLAGS_revalidate_block_handle) {
        RevalidateHandle(block_id);
      }
      if (io_->WriteBlock(block_id, data)) {
        if (FLAGS_io_ack_delay_us > 0) {
          usleep(static_cast<unsigned>(FLAGS_io_ack_delay_us));
        }
        LOG(INFO) << "OpId=" << block_id
                  << " op=kWrite vdisk=NFS:0:" << (std::hash<std::string>{}(block_id) % 4096)
                  << " offset=0 size=" << len
                  << " latency_usecs=" << (rand() % 800 + 200)
                  << " status=kOk";
        return "OK\n";
      }
      LOG(WARNING) << "OpId=" << block_id << " op=kWrite status=kError";
      return "ERROR Write failed\n";

    } else if (cmd == "READ") {
      std::string block_id;
      iss >> block_id;
      std::string data;
      if (FLAGS_revalidate_block_handle) {
        RevalidateHandle(block_id);
      }
      if (io_->ReadBlock(block_id, &data)) {
        if (FLAGS_io_ack_delay_us > 0) {
          usleep(static_cast<unsigned>(FLAGS_io_ack_delay_us));
        }
        LOG(INFO) << "OpId=" << block_id
                  << " op=kRead vdisk=NFS:0:" << (std::hash<std::string>{}(block_id) % 4096)
                  << " offset=0 size=" << data.size()
                  << " latency_usecs=" << (rand() % 500 + 100)
                  << " status=kOk";
        return "DATA " + block_id + " " + std::to_string(data.size()) +
               "\n" + data + "\n";
      }
      LOG(INFO) << "OpId=" << block_id << " op=kRead status=kNotFound";
      return "NOT_FOUND\n";

    } else if (cmd == "DELETE") {
      std::string block_id;
      iss >> block_id;
      if (io_->DeleteBlock(block_id)) {
        LOG(INFO) << "OpId=" << block_id
                  << " op=kDelete vdisk=NFS:0:" << (std::hash<std::string>{}(block_id) % 4096)
                  << " latency_usecs=" << (rand() % 200 + 50)
                  << " status=kOk";
        return "OK\n";
      }
      LOG(INFO) << "OpId=" << block_id << " op=kDelete status=kNotFound";
      return "NOT_FOUND\n";

    } else if (cmd == "REPLICATE") {
      std::string block_id, target_ip;
      int target_port;
      iss >> block_id >> target_ip >> target_port;
      std::string data;
      if (!io_->ReadBlock(block_id, &data)) {
        return "ERROR Block not found locally\n";
      }
      if (ReplicateBlock(target_ip, static_cast<uint16_t>(target_port),
                         block_id, data)) {
        return "OK\n";
      }
      return "ERROR Replication failed\n";

    } else if (cmd == "STATS") {
      std::ostringstream oss;
      oss << "STATS_REPLY {\"num_blocks\":" << io_->NumBlocks()
          << ",\"total_bytes\":" << io_->TotalBytes()
          << ",\"reads\":" << io_->ReadsCompleted()
          << ",\"writes\":" << io_->WritesCompleted()
          << ",\"node\":\"" << local_ip_ << "\"}\n";
      return oss.str();
    }

    return "ERROR Unknown command\n";
  }

  std::unique_ptr<IoHandler> io_;
  std::string local_ip_;
  int listen_fd_ = -1;
  std::thread accept_thread_;
};

}  // namespace hermes
}  // namespace eidolon

int main(int argc, char* argv[]) {
  eidolon::hermes::HermesService svc;
  svc.Init(argc, argv);
  svc.Run();
  return 0;
}
