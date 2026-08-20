#include "misc_common.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

static int ListenLocal(int port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    perror("bind");
    close(fd);
    return -1;
  }
  if (listen(fd, 128) < 0) {
    perror("listen");
    close(fd);
    return -1;
  }
  return fd;
}

static int ConnectLocal(int port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

int main(int argc, char** argv) {
  InstallSignals();
  const std::string comm = GetStrArg(argc, argv, "--comm", "health-probe");
  SetComm(comm.c_str());

  int port = GetIntArg(argc, argv, "--port", 19080);
  int clients = GetIntArg(argc, argv, "--clients", 40);
  if (port < 1024) {
    port = 19080;
  }
  if (clients < 1) {
    clients = 1;
  }
  if (clients > 200) {
    clients = 200;
  }

  const int listen_fd = ListenLocal(port);
  if (listen_fd < 0) {
    return 1;
  }
  std::fprintf(stderr, "health-probe: listening on 127.0.0.1:%d, clients=%d\n",
               port, clients);

  std::mutex accepted_mu;
  std::vector<int> accepted;
  accepted.reserve(static_cast<size_t>(clients));
  std::thread acceptor([&] {
    while (g_running.load(std::memory_order_relaxed)) {
      sockaddr_in peer {};
      socklen_t plen = sizeof(peer);
      const int cfd =
          accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &plen);
      if (cfd < 0) {
        break;
      }
      std::lock_guard<std::mutex> lock(accepted_mu);
      accepted.push_back(cfd);
    }
  });

  std::vector<int> client_fds;
  client_fds.reserve(static_cast<size_t>(clients));
  for (int i = 0; i < clients && g_running.load(std::memory_order_relaxed);
       ++i) {
    const int fd = ConnectLocal(port);
    if (fd < 0) {
      std::fprintf(stderr, "health-probe: client %d failed to connect\n", i);
      SleepMs(50);
      continue;
    }
    client_fds.push_back(fd);
  }
  std::fprintf(stderr, "health-probe: %zu client sockets held\n",
               client_fds.size());

  while (g_running.load(std::memory_order_relaxed)) {
    SleepMs(500);
  }

  shutdown(listen_fd, SHUT_RDWR);
  close(listen_fd);
  for (int fd : client_fds) {
    close(fd);
  }
  {
    std::lock_guard<std::mutex> lock(accepted_mu);
    for (int fd : accepted) {
      close(fd);
    }
  }
  if (acceptor.joinable()) {
    acceptor.join();
  }
  return 0;
}
