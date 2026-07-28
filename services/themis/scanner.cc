#include "scanner.h"
#include "eidolon/net.h"
#include "eidolon/protocol.h"

#include <glog/logging.h>

#include <cmath>
#include <sstream>

namespace eidolon {
namespace themis {

Scanner::Scanner(const std::string& styx_host, uint16_t styx_port)
    : styx_host_(styx_host), styx_port_(styx_port) {}

bool Scanner::RunScan() {
  results_.clear();
  under_replicated_.clear();

  // Query Styx for the list of Hermes endpoints.
  int fd = net::TcpConnect(styx_host_, styx_port_);
  if (fd < 0) {
    LOG(ERROR) << "Scanner: cannot connect to Styx";
    return false;
  }
  std::string cmd = "LOOKUP hermes\n";
  net::TcpSendAll(fd, cmd.data(), cmd.size());

  char buf[4096];
  ssize_t n = net::TcpRecv(fd, buf, sizeof(buf));
  net::Close(fd);
  if (n <= 0) {
    LOG(ERROR) << "Scanner: empty response from Styx";
    return false;
  }
  std::string resp(buf, n);

  // Parse ENDPOINTS response.
  std::istringstream iss(resp);
  std::string header;
  int count = 0;
  iss >> header >> count;

  struct Endpoint { std::string ip; uint16_t port; };
  std::vector<Endpoint> endpoints;
  for (int i = 0; i < count; ++i) {
    std::string ep;
    iss >> ep;
    auto colon = ep.find(':');
    if (colon == std::string::npos) continue;
    endpoints.push_back({ep.substr(0, colon),
                         static_cast<uint16_t>(std::stoi(ep.substr(colon + 1)))});
  }

  // Query STATS from each Hermes.
  for (auto& ep : endpoints) {
    int hfd = net::TcpConnect(ep.ip, ep.port);
    if (hfd < 0) {
      LOG(WARNING) << "Scanner: cannot reach Hermes at " << ep.ip;
      continue;
    }
    std::string stats_cmd = "STATS\n";
    net::TcpSendAll(hfd, stats_cmd.data(), stats_cmd.size());
    char sbuf[4096];
    ssize_t sn = net::TcpRecv(hfd, sbuf, sizeof(sbuf));
    net::Close(hfd);
    if (sn <= 0) continue;

    // Very simple JSON field extraction.
    std::string s(sbuf, sn);
    NodeStats ns;
    ns.ip = ep.ip;
    auto extract = [&](const std::string& key) -> uint64_t {
      auto pos = s.find("\"" + key + "\":");
      if (pos == std::string::npos) return 0;
      pos = s.find(':', pos) + 1;
      return std::stoull(s.substr(pos));
    };
    ns.num_blocks = extract("num_blocks");
    ns.total_bytes = extract("total_bytes");
    results_.push_back(ns);
  }

  LOG(INFO) << "Scan complete: " << results_.size() << " nodes surveyed";
  return true;
}

bool Scanner::IsImbalanced(double threshold_pct) const {
  if (results_.size() < 2) return false;
  uint64_t total = 0;
  for (auto& ns : results_) total += ns.total_bytes;
  double avg = static_cast<double>(total) / results_.size();
  if (avg == 0) return false;

  for (auto& ns : results_) {
    double deviation = std::abs(static_cast<double>(ns.total_bytes) - avg) / avg * 100.0;
    if (deviation > threshold_pct) return true;
  }
  return false;
}

}  // namespace themis
}  // namespace eidolon
