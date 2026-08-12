#include "vm_table.h"
#include "scheduler.h"
#include "eidolon/service_base.h"
#include "eidolon/net.h"
#include "eidolon/protocol.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>
#include <sstream>
#include <thread>
#include <vector>

DEFINE_int32(port, eidolon::kDefaultAtlasPort, "TCP listen port");
DEFINE_int32(scheduling_depth, 10,
    "Maximum VM placement search depth per scheduling cycle");
DEFINE_int32(vm_table_size_mb, 4,
    "Size of the in-memory VM state table for placement lookups");
DEFINE_string(health_check_targets, "hermes",
    "Comma-separated services to health-check each cycle");
DEFINE_int32(scheduling_interval_ms, 5000,
    "Milliseconds between VM scheduling cycles");
DEFINE_int32(table_lookups_per_cycle, 100,
    "Number of random table lookups per scheduling cycle");
DEFINE_string(styx_host, "127.0.0.1", "Styx host for registration");
DEFINE_int32(styx_port, eidolon::kDefaultStyxPort, "Styx TCP port");

namespace eidolon {
namespace atlas {

class AtlasService : public ServiceBase {
 public:
  AtlasService() : ServiceBase("atlas", "", "", "") {}

 protected:
  void OnStart() override {
    local_ip_ = net::GetLocalIp();
    table_ = std::make_unique<VmTable>(
        static_cast<size_t>(FLAGS_vm_table_size_mb));
    scheduler_ = std::make_unique<Scheduler>(FLAGS_scheduling_depth);

    listen_fd_ = net::TcpListen(static_cast<uint16_t>(FLAGS_port));
    if (listen_fd_ < 0) {
      LOG(FATAL) << "Failed to listen on port " << FLAGS_port;
    }

    LOG(INFO) << "Atlas listening on port " << FLAGS_port
              << " (scheduling_depth=" << FLAGS_scheduling_depth
              << ", vm_table_size_mb=" << FLAGS_vm_table_size_mb
              << ", table_entries=" << table_->Size()
              << ", health_targets=" << FLAGS_health_check_targets
              << ", interval_ms=" << FLAGS_scheduling_interval_ms << ")";

    accept_thread_ = std::thread([this] { AcceptLoop(); });
    schedule_thread_ = std::thread([this] { ScheduleLoop(); });
    RegisterWithStyx();
  }

  void OnStop() override {
    net::Close(listen_fd_);
    if (accept_thread_.joinable()) accept_thread_.join();
    if (schedule_thread_.joinable()) schedule_thread_.join();
  }

 private:
  void RegisterWithStyx() {
    int fd = net::TcpConnect(FLAGS_styx_host,
                             static_cast<uint16_t>(FLAGS_styx_port));
    if (fd < 0) {
      LOG(WARNING) << "Cannot connect to Styx for registration";
      return;
    }
    std::string cmd = "REG atlas " + local_ip_ + " " +
                      std::to_string(FLAGS_port) + "\n";
    net::TcpSendAll(fd, cmd.data(), cmd.size());
    net::Close(fd);
  }

  void AcceptLoop() {
    while (IsRunning()) {
      std::string peer_ip;
      int client = net::TcpAccept(listen_fd_, &peer_ip);
      if (client < 0) {
        if (IsRunning())
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      std::thread(&AtlasService::HandleClient, this, client, peer_ip).detach();
    }
  }

  void HandleClient(int fd, const std::string& peer) {
    std::string buf;
    char chunk[4096];
    while (IsRunning()) {
      ssize_t n = net::TcpRecv(fd, chunk, sizeof(chunk));
      if (n <= 0) break;
      buf.append(chunk, n);

      size_t pos;
      while ((pos = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, pos);
        buf.erase(0, pos + 1);
        std::string response = ProcessCommand(line);
        if (!response.empty()) {
          net::TcpSendAll(fd, response.data(), response.size());
        }
      }
    }
    net::Close(fd);
  }

  std::string ProcessCommand(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "STATS") {
      std::ostringstream oss;
      oss << "STATS_REPLY {"
          << "\"total_vms\":" << table_->Size()
          << ",\"unplaced_vms\":" << table_->UnplacedCount()
          << ",\"scheduling_depth\":" << FLAGS_scheduling_depth
          << ",\"vm_table_size_mb\":" << FLAGS_vm_table_size_mb
          << ",\"cycles_completed\":" << cycles_completed_
          << ",\"last_cycle_ms\":" << last_cycle_ms_
          << ",\"health_checks_ok\":" << health_ok_
          << ",\"health_checks_fail\":" << health_fail_
          << ",\"node\":\"" << local_ip_ << "\"}\n";
      return oss.str();
    }

    return "ERROR Unknown command\n";
  }

  std::vector<std::string> ParseTargets() {
    std::vector<std::string> targets;
    std::istringstream ss(FLAGS_health_check_targets);
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (!token.empty()) targets.push_back(token);
    }
    return targets;
  }

  void HealthCheck(const std::string& service) {
    int fd = net::TcpConnect(FLAGS_styx_host,
                             static_cast<uint16_t>(FLAGS_styx_port));
    if (fd < 0) {
      LOG(WARNING) << "health_check service=" << service
                   << " styx_connect=failed";
      ++health_fail_;
      return;
    }

    std::string lookup = "LOOKUP " + service + "\n";
    net::TcpSendAll(fd, lookup.data(), lookup.size());

    char resp[4096];
    ssize_t n = net::TcpRecv(fd, resp, sizeof(resp));
    net::Close(fd);

    if (n <= 0) {
      LOG(WARNING) << "health_check service=" << service
                   << " lookup=empty";
      ++health_fail_;
      return;
    }

    std::string data(resp, n);
    std::istringstream iss(data);
    std::string line;
    int checked = 0, ok = 0;

    while (std::getline(iss, line)) {
      if (line.empty() || !std::isdigit(line[0])) continue;
      auto colon = line.find(':');
      if (colon == std::string::npos) continue;
      std::string ip = line.substr(0, colon);
      int port = std::stoi(line.substr(colon + 1));

      int sfd = net::TcpConnect(ip, static_cast<uint16_t>(port));
      if (sfd < 0) {
        LOG(WARNING) << "health_check service=" << service
                     << " node=" << ip << ":" << port << " connect=failed";
        ++health_fail_;
        ++checked;
        continue;
      }
      std::string stats_cmd = "STATS\n";
      net::TcpSendAll(sfd, stats_cmd.data(), stats_cmd.size());
      char sr[2048];
      ssize_t sn = net::TcpRecv(sfd, sr, sizeof(sr));
      net::Close(sfd);

      if (sn > 0) {
        ++ok;
        ++health_ok_;
      } else {
        ++health_fail_;
      }
      ++checked;
    }

    LOG(INFO) << "health_check service=" << service
              << " endpoints=" << checked << " ok=" << ok;
  }

  void ScheduleLoop() {
    while (IsRunning()) {
      auto start = std::chrono::steady_clock::now();

      // 1. Placement scheduling (CPU-bound at high depths).
      std::vector<NodeResources> nodes;
      NodeResources self;
      self.ip = local_ip_;
      nodes.push_back(self);

      size_t placed = scheduler_->RunCycle(*table_, nodes);

      // 2. Random table lookups (memory-bound at large table sizes).
      uint64_t key = static_cast<uint64_t>(
          std::chrono::steady_clock::now().time_since_epoch().count());
      table_->RandomLookup(key,
          static_cast<size_t>(FLAGS_table_lookups_per_cycle));

      // 3. Health-check target services (network I/O).
      auto targets = ParseTargets();
      for (auto& t : targets) {
        HealthCheck(t);
      }

      auto elapsed = std::chrono::steady_clock::now() - start;
      last_cycle_ms_ = std::chrono::duration_cast<
          std::chrono::milliseconds>(elapsed).count();
      ++cycles_completed_;

      LOG(INFO) << "schedule_cycle=" << cycles_completed_
                << " placed=" << placed
                << " unplaced=" << table_->UnplacedCount()
                << " lookups=" << FLAGS_table_lookups_per_cycle
                << " cycle_ms=" << last_cycle_ms_;

      // Sleep the remainder of the interval.
      auto sleep_ms = FLAGS_scheduling_interval_ms - last_cycle_ms_;
      if (sleep_ms > 0) {
        for (int i = 0; i < sleep_ms && IsRunning(); i += 200) {
          std::this_thread::sleep_for(std::chrono::milliseconds(
              std::min(200, static_cast<int>(sleep_ms - i))));
        }
      }
    }
  }

  std::unique_ptr<VmTable> table_;
  std::unique_ptr<Scheduler> scheduler_;
  std::string local_ip_;
  int listen_fd_ = -1;
  std::thread accept_thread_;
  std::thread schedule_thread_;
  uint64_t cycles_completed_ = 0;
  int64_t last_cycle_ms_ = 0;
  uint64_t health_ok_ = 0;
  uint64_t health_fail_ = 0;
};

}  // namespace atlas
}  // namespace eidolon

int main(int argc, char* argv[]) {
  eidolon::atlas::AtlasService svc;
  svc.Init(argc, argv);
  svc.Run();
  return 0;
}
