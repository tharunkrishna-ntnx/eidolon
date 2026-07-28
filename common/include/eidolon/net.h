#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace eidolon {
namespace net {

// Create a TCP server socket bound to |port| on all interfaces.
// Returns the file descriptor or -1 on error.
int TcpListen(uint16_t port, int backlog = 128);

// Accept a connection on |listen_fd|.  Writes the peer address into
// |peer_ip| and |peer_port| if non-null.  Returns the new fd or -1.
int TcpAccept(int listen_fd, std::string* peer_ip = nullptr,
              uint16_t* peer_port = nullptr);

// Connect to a TCP endpoint.  Returns the fd or -1.
int TcpConnect(const std::string& host, uint16_t port,
               int timeout_ms = 5000);

// Send all bytes.  Returns true on success.
bool TcpSendAll(int fd, const void* data, size_t len);

// Receive up to |len| bytes.  Returns bytes read, 0 on EOF, -1 on error.
ssize_t TcpRecv(int fd, void* buf, size_t len);

// Send a framed message (length-prefixed).
bool SendFrame(int fd, const std::string& payload);

// Receive a framed message.  Returns true on success.
bool RecvFrame(int fd, std::string& out, int timeout_ms = 30000);

// Close a file descriptor safely.
void Close(int fd);

// Create a UDP socket for broadcast, bound to |port|.
// |enable_broadcast| sets SO_BROADCAST.  |reuse| sets SO_REUSEADDR.
int UdpBroadcastSocket(uint16_t port, bool enable_broadcast = true,
                       bool reuse = true);

// Send a UDP broadcast datagram on the subnet broadcast address.
bool UdpBroadcast(int fd, uint16_t port, const void* data, size_t len);

// Receive a UDP datagram.  Writes sender address if pointers are non-null.
ssize_t UdpRecv(int fd, void* buf, size_t max_len,
                std::string* sender_ip = nullptr,
                uint16_t* sender_port = nullptr,
                int timeout_ms = 1000);

// Return the first non-loopback IPv4 address of this host.
std::string GetLocalIp();

}  // namespace net
}  // namespace eidolon
