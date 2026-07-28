#include "scanner.h"
#include "rebalancer.h"
#include "eidolon/service_base.h"
#include "eidolon/net.h"
#include "eidolon/protocol.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

DEFINE_int32(scan_interval_sec, 60, "Seconds between scans");
DEFINE_double(imbalance_threshold_pct, 20.0, "Percent deviation to trigger rebalance");
DEFINE_string(styx_host, "127.0.0.1", "Styx host");
DEFINE_int32(styx_port, eidolon::kDefaultStyxPort, "Styx TCP port");
DEFINE_int32(port, 0, "TCP port (unused by Themis, reserved for future API)");

namespace eidolon {
namespace themis {

class ThemisService : public ServiceBase {
 public:
  ThemisService() : ServiceBase("themis", "", "", "") {}

 protected:
  void OnStart() override {
    local_ip_ = net::GetLocalIp();
    LOG(INFO) << "Themis starting, scan_interval=" << FLAGS_scan_interval_sec
              << "s, threshold=" << FLAGS_imbalance_threshold_pct << "%";
    scan_thread_ = std::thread([this] { ScanLoop(); });
    RegisterWithStyx();
  }

  void OnStop() override {
    if (scan_thread_.joinable()) scan_thread_.join();
  }

 private:
  void RegisterWithStyx() {
    int fd = net::TcpConnect(FLAGS_styx_host,
                             static_cast<uint16_t>(FLAGS_styx_port));
    if (fd < 0) {
      LOG(WARNING) << "Cannot connect to Styx for registration";
      return;
    }
    std::string cmd = "REG themis " + local_ip_ + " 0\n";
    net::TcpSendAll(fd, cmd.data(), cmd.size());
    net::Close(fd);
  }

  void ScanLoop() {
    while (IsRunning()) {
      // Wait for the scan interval.
      for (int i = 0; i < FLAGS_scan_interval_sec * 5 && IsRunning(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      if (!IsRunning()) break;

      LOG(INFO) << "Starting Themis scan";
      Scanner scanner(FLAGS_styx_host,
                      static_cast<uint16_t>(FLAGS_styx_port));
      if (!scanner.RunScan()) {
        LOG(WARNING) << "Scan failed, will retry next interval";
        continue;
      }

      WriteScanReport(scanner);

      if (scanner.IsImbalanced(FLAGS_imbalance_threshold_pct)) {
        LOG(WARNING) << "Data imbalance detected, planning rebalance";
        Rebalancer rb;
        auto ops = rb.Plan(scanner.Results());
        for (auto& op : ops) {
          if (!IsRunning()) break;
          rb.Execute(op);
        }
      } else {
        LOG(INFO) << "Cluster data is balanced";
      }
    }
  }

  void WriteScanReport(const Scanner& scanner) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::localtime(&t), "%Y%m%d-%H%M%S");

    // Write to log_dir if available, otherwise current directory.
    std::string report_path = "themis_scan_" + ts.str() + ".log";
    // The ServiceBase doesn't expose log_dir, so we write relative.
    // In production, Prometheus passes --log_dir via gflags.
    std::ofstream ofs(report_path);
    ofs << "Themis Scan Report - " << ts.str() << "\n";
    ofs << "---\n";
    for (auto& ns : scanner.Results()) {
      ofs << "Node: " << ns.ip
          << "  blocks=" << ns.num_blocks
          << "  bytes=" << ns.total_bytes << "\n";
    }
    ofs << "Imbalanced: " << (scanner.IsImbalanced() ? "YES" : "NO") << "\n";
    LOG(INFO) << "Scan report written: " << report_path;
  }

  std::string local_ip_;
  std::thread scan_thread_;
};

}  // namespace themis
}  // namespace eidolon

int main(int argc, char* argv[]) {
  eidolon::themis::ThemisService svc;
  svc.Init(argc, argv);
  svc.Run();
  return 0;
}
