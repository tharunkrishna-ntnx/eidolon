#include "eidolon/net.h"
#include "eidolon/protocol.h"

#include <glog/logging.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace eidolon {
namespace net {

int TcpListen(uint16_t port, int backlog) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    PLOG(ERROR) << "socket()";
    return -1;
  }

  int opt = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    PLOG(ERROR) << "bind(" << port << ")";
    ::close(fd);
    return -1;
  }
  if (::listen(fd, backlog) < 0) {
    PLOG(ERROR) << "listen()";
    ::close(fd);
    return -1;
  }
  return fd;
}

int TcpAccept(int listen_fd, std::string* peer_ip, uint16_t* peer_port) {
  sockaddr_in peer{};
  socklen_t len = sizeof(peer);
  int fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &len);
  if (fd < 0) return -1;
  if (peer_ip) {
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer.sin_addr, buf, sizeof(buf));
    *peer_ip = buf;
  }
  if (peer_port) *peer_port = ntohs(peer.sin_port);
  return fd;
}

int TcpConnect(const std::string& host, uint16_t port, int timeout_ms) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    return -1;
  }

  // Non-blocking connect with timeout.
  struct timeval tv;
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool TcpSendAll(int fd, const void* data, size_t len) {
  auto* p = static_cast<const char*>(data);
  while (len > 0) {
    ssize_t n = ::send(fd, p, len, MSG_NOSIGNAL);
    if (n <= 0) return false;
    p += n;
    len -= static_cast<size_t>(n);
  }
  return true;
}

ssize_t TcpRecv(int fd, void* buf, size_t len) {
  return ::recv(fd, buf, len, 0);
}

bool SendFrame(int fd, const std::string& payload) {
  std::string frame = FrameMessage(payload);
  return TcpSendAll(fd, frame.data(), frame.size());
}

bool RecvFrame(int fd, std::string& out, int timeout_ms) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;

  // Read the 4-byte length header.
  uint32_t net_len = 0;
  size_t got = 0;
  while (got < 4) {
    int r = ::poll(&pfd, 1, timeout_ms);
    if (r <= 0) return false;
    ssize_t n = ::recv(fd, reinterpret_cast<char*>(&net_len) + got, 4 - got, 0);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  uint32_t payload_len = ntohl(net_len);
  if (payload_len > kMaxPayloadBytes) return false;

  out.resize(payload_len);
  got = 0;
  while (got < payload_len) {
    int r = ::poll(&pfd, 1, timeout_ms);
    if (r <= 0) return false;
    ssize_t n = ::recv(fd, &out[got], payload_len - got, 0);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

void Close(int fd) {
  if (fd >= 0) ::close(fd);
}

int UdpBroadcastSocket(uint16_t port, bool enable_broadcast, bool reuse) {
  int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return -1;

  if (enable_broadcast) {
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
  }
  if (reuse) {
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    PLOG(ERROR) << "bind(udp " << port << ")";
    ::close(fd);
    return -1;
  }
  return fd;
}

bool UdpBroadcast(int fd, uint16_t port, const void* data, size_t len) {
  sockaddr_in bcast{};
  bcast.sin_family = AF_INET;
  bcast.sin_addr.s_addr = INADDR_BROADCAST;
  bcast.sin_port = htons(port);
  ssize_t n = ::sendto(fd, data, len, 0,
                       reinterpret_cast<sockaddr*>(&bcast), sizeof(bcast));
  return n == static_cast<ssize_t>(len);
}

ssize_t UdpRecv(int fd, void* buf, size_t max_len,
                std::string* sender_ip, uint16_t* sender_port,
                int timeout_ms) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  int r = ::poll(&pfd, 1, timeout_ms);
  if (r <= 0) return r;  // 0 = timeout, -1 = error

  sockaddr_in from{};
  socklen_t from_len = sizeof(from);
  ssize_t n = ::recvfrom(fd, buf, max_len, 0,
                         reinterpret_cast<sockaddr*>(&from), &from_len);
  if (n > 0) {
    if (sender_ip) {
      char ip_buf[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &from.sin_addr, ip_buf, sizeof(ip_buf));
      *sender_ip = ip_buf;
    }
    if (sender_port) *sender_port = ntohs(from.sin_port);
  }
  return n;
}

std::string GetLocalIp() {
  struct ifaddrs* ifas = nullptr;
  if (::getifaddrs(&ifas) != 0) return "127.0.0.1";

  std::string result = "127.0.0.1";
  for (auto* ifa = ifas; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
    auto* sa = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
    uint32_t ip = ntohl(sa->sin_addr.s_addr);
    if ((ip >> 24) == 127) continue;  // skip loopback
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
    result = buf;
    break;
  }
  ::freeifaddrs(ifas);
  return result;
}

}  // namespace net
}  // namespace eidolon

// --- protocol helpers ---
namespace eidolon {

std::string FrameMessage(const std::string& payload) {
  uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
  std::string frame(4 + payload.size(), '\0');
  std::memcpy(&frame[0], &len, 4);
  std::memcpy(&frame[4], payload.data(), payload.size());
  return frame;
}

bool ExtractFrame(std::string& buf, std::string& out) {
  if (buf.size() < 4) return false;
  uint32_t net_len;
  std::memcpy(&net_len, buf.data(), 4);
  uint32_t payload_len = ntohl(net_len);
  if (payload_len > kMaxPayloadBytes) {
    buf.clear();
    return false;
  }
  if (buf.size() < 4 + payload_len) return false;
  out.assign(buf, 4, payload_len);
  buf.erase(0, 4 + payload_len);
  return true;
}

}  // namespace eidolon
