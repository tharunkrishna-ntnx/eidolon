#include "misc_common.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  InstallSignals();
  const std::string comm = GetStrArg(argc, argv, "--comm", "log-shipper");
  SetComm(comm.c_str());

  const std::string path =
      GetStrArg(argc, argv, "--path", "data/log-shipper.dat");
  int cpu_ms = GetIntArg(argc, argv, "--cpu-ms", 200);
  int write_kb = GetIntArg(argc, argv, "--write-kb", 256);
  const bool do_fsync = HasFlag(argc, argv, "--fsync");
  if (cpu_ms < 10) {
    cpu_ms = 10;
  }
  if (write_kb < 4) {
    write_kb = 4;
  }
  if (write_kb > 4096) {
    write_kb = 4096;
  }

  const int fd =
      open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  std::vector<char> buf(static_cast<size_t>(write_kb) * 1024, 'L');
  std::fprintf(stderr,
               "log-shipper: path=%s cpu_ms=%d write_kb=%d fsync=%s\n",
               path.c_str(), cpu_ms, write_kb, do_fsync ? "yes" : "no");

  while (g_running.load(std::memory_order_relaxed)) {
    BurnMs(cpu_ms, g_running);
    if (!g_running.load(std::memory_order_relaxed)) {
      break;
    }
    const ssize_t n = write(fd, buf.data(), buf.size());
    if (n < 0) {
      perror("write");
      break;
    }
    if (do_fsync) {
      fsync(fd);
    }
    SleepMs(50);
  }

  close(fd);
  unlink(path.c_str());
  return 0;
}
