#include "rebalancer.h"
#include "eidolon/net.h"

#include <glog/logging.h>

#include <algorithm>
#include <sstream>

namespace eidolon {
namespace themis {

std::vector<Rebalancer::MoveOp> Rebalancer::Plan(
    const std::vector<NodeStats>& stats) const {
  // Simplified planning: if a node has more than avg * 1.2 bytes, move blocks
  // to the node with the least data.  Real implementation would be more
  // sophisticated.
  std::vector<Rebalancer::MoveOp> ops;
  if (stats.size() < 2) return ops;

  uint64_t total = 0;
  for (auto& ns : stats) total += ns.total_bytes;
  uint64_t avg = total / stats.size();

  auto sorted = stats;
  std::sort(sorted.begin(), sorted.end(),
            [](auto& a, auto& b) { return a.total_bytes > b.total_bytes; });

  // For now, just log the plan rather than generating specific block moves
  // (we'd need the block list from each Hermes for that).
  for (auto& ns : sorted) {
    if (ns.total_bytes > avg * 12 / 10) {
      LOG(INFO) << "Rebalancer: node " << ns.ip << " is overloaded ("
                << ns.total_bytes << " bytes vs avg " << avg << ")";
    }
  }
  return ops;
}

bool Rebalancer::Execute(const MoveOp& op) const {
  int fd = net::TcpConnect(op.from_ip, op.from_port);
  if (fd < 0) {
    LOG(ERROR) << "Rebalancer: cannot connect to source " << op.from_ip;
    return false;
  }

  std::ostringstream oss;
  oss << "REPLICATE " << op.block_id << " " << op.to_ip << " "
      << op.to_port << "\n";
  std::string cmd = oss.str();
  net::TcpSendAll(fd, cmd.data(), cmd.size());

  char buf[64];
  ssize_t n = net::TcpRecv(fd, buf, sizeof(buf));
  net::Close(fd);

  if (n > 0) {
    std::string resp(buf, n);
    return resp.find("OK") != std::string::npos;
  }
  return false;
}

}  // namespace themis
}  // namespace eidolon
