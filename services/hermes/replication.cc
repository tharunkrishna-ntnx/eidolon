#include "replication.h"
#include "eidolon/net.h"

#include <glog/logging.h>
#include <sstream>

namespace eidolon {
namespace hermes {

bool ReplicateBlock(const std::string& target_ip, uint16_t target_port,
                    const std::string& block_id, const std::string& data) {
  int fd = net::TcpConnect(target_ip, target_port);
  if (fd < 0) {
    LOG(WARNING) << "Cannot connect to peer " << target_ip << ":"
                 << target_port << " for replication";
    return false;
  }

  std::ostringstream oss;
  oss << "WRITE " << block_id << " " << data.size() << "\n";
  std::string header = oss.str();

  bool ok = net::TcpSendAll(fd, header.data(), header.size()) &&
            net::TcpSendAll(fd, data.data(), data.size()) &&
            net::TcpSendAll(fd, "\n", 1);

  if (ok) {
    // Read response.
    char buf[64];
    ssize_t n = net::TcpRecv(fd, buf, sizeof(buf));
    if (n > 0) {
      std::string resp(buf, n);
      ok = resp.find("OK") != std::string::npos;
    } else {
      ok = false;
    }
  }

  net::Close(fd);
  if (!ok) {
    LOG(WARNING) << "Replication of block " << block_id << " to "
                 << target_ip << " failed";
  }
  return ok;
}

}  // namespace hermes
}  // namespace eidolon
