#include "misc_common.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  InstallSignals();
  const std::string comm = GetStrArg(argc, argv, "--comm", "page-scanner");
  SetComm(comm.c_str());

  const std::string path =
      GetStrArg(argc, argv, "--path", "data/page-scanner.dat");
  int file_mb = GetIntArg(argc, argv, "--file-mb", 64);
  int write_kb = GetIntArg(argc, argv, "--write-kb", 256);
  const bool do_fsync = HasFlag(argc, argv, "--fsync");
  const bool do_mmap = HasFlag(argc, argv, "--mmap");
  if (file_mb < 8) {
    file_mb = 8;
  }
  if (file_mb > 256) {
    file_mb = 256;
  }
  if (write_kb < 16) {
    write_kb = 16;
  }
  if (write_kb > 4096) {
    write_kb = 4096;
  }

  const int fd =
      open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    perror("open");
    return 1;
  }

  const size_t file_bytes = static_cast<size_t>(file_mb) * 1024ULL * 1024ULL;
  if (ftruncate(fd, static_cast<off_t>(file_bytes)) != 0) {
    perror("ftruncate");
    close(fd);
    return 1;
  }

  void* map = MAP_FAILED;
  if (do_mmap) {
    map = mmap(nullptr, file_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
      perror("mmap");
    }
  }

  std::vector<char> buf(static_cast<size_t>(write_kb) * 1024, 'P');
  std::fprintf(stderr,
               "page-scanner: path=%s file_mb=%d write_kb=%d fsync=%s mmap=%s\n",
               path.c_str(), file_mb, write_kb, do_fsync ? "yes" : "no",
               do_mmap ? "yes" : "no");

  off_t off = 0;
  size_t touch = 0;
  while (g_running.load(std::memory_order_relaxed)) {
    if (lseek(fd, off, SEEK_SET) < 0) {
      off = 0;
      continue;
    }
    const ssize_t n = write(fd, buf.data(), buf.size());
    if (n < 0) {
      perror("write");
      break;
    }
    off += n;
    if (static_cast<size_t>(off) + buf.size() > file_bytes) {
      off = 0;
      if (do_fsync) {
        fsync(fd);
      }
    }

    if (do_mmap && map != MAP_FAILED) {
      auto* p = static_cast<volatile char*>(map);
      // Walk pages to generate faults / cache traffic.
      for (size_t i = 0; i < file_bytes; i += 4096) {
        if (!g_running.load(std::memory_order_relaxed)) {
          break;
        }
        p[i] = static_cast<char>(p[i] + 1);
        ++touch;
      }
    }
  }

  if (map != MAP_FAILED) {
    munmap(map, file_bytes);
  }
  close(fd);
  unlink(path.c_str());
  (void)touch;
  return 0;
}
