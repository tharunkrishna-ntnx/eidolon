#include "misc_common.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

int main(int argc, char** argv) {
  InstallSignals();
  const std::string comm = GetStrArg(argc, argv, "--comm", "index-builder");
  SetComm(comm.c_str());

  int mb_per_sec = GetIntArg(argc, argv, "--mb-per-sec", 8);
  int cap_mb = GetIntArg(argc, argv, "--cap-mb", 512);
  int start_mb = GetIntArg(argc, argv, "--start-mb", 32);
  if (mb_per_sec < 1) {
    mb_per_sec = 1;
  }
  if (cap_mb < 1) {
    cap_mb = 1;
  }
  if (cap_mb > 1024) {
    cap_mb = 1024;
  }

  if (start_mb < 0) {
    start_mb = 0;
  }
  if (start_mb > cap_mb) {
    start_mb = cap_mb;
  }

  std::fprintf(stderr, "index-builder: leak %d MB/s, cap %d MB, start %d MB\n",
               mb_per_sec, cap_mb, start_mb);

  std::vector<char*> chunks;
  chunks.reserve(static_cast<size_t>(cap_mb) + 8);
  int allocated_mb = 0;

  auto alloc_one_mb = [&]() -> bool {
    const size_t bytes = 1024ULL * 1024ULL;
    char* p = nullptr;
    try {
      p = new char[bytes];
    } catch (const std::bad_alloc&) {
      std::fprintf(stderr, "index-builder: allocation failed at %d MB\n",
                   allocated_mb);
      return false;
    }
    std::memset(p, 0xAB, bytes);
    chunks.push_back(p);
    ++allocated_mb;
    return true;
  };

  while (g_running.load(std::memory_order_relaxed) && allocated_mb < start_mb) {
    if (!alloc_one_mb()) {
      goto hold;
    }
  }
  if (start_mb > 0) {
    std::fprintf(stderr, "index-builder: starting RSS ~%d MB\n", allocated_mb);
  }

  while (g_running.load(std::memory_order_relaxed) && allocated_mb < cap_mb) {
    const int remaining = cap_mb - allocated_mb;
    const int this_mb = remaining < mb_per_sec ? remaining : mb_per_sec;
    for (int i = 0; i < this_mb; ++i) {
      if (!alloc_one_mb()) {
        goto hold;
      }
    }
    std::fprintf(stderr, "index-builder: RSS ~%d MB\n", allocated_mb);
    if (allocated_mb < cap_mb) {
      SleepMs(1000);
    }
  }

hold:
  std::fprintf(stderr, "index-builder: holding ~%d MB (cap reached or stop)\n",
               allocated_mb);
  while (g_running.load(std::memory_order_relaxed)) {
    SleepMs(500);
  }

  for (char* p : chunks) {
    delete[] p;
  }
  return 0;
}
