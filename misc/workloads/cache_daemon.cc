#include "misc_common.h"

#include <cstdio>
#include <new>

int main(int argc, char** argv) {
  InstallSignals();
  const std::string comm = GetStrArg(argc, argv, "--comm", "cache-daemon");
  SetComm(comm.c_str());

  int mb = GetIntArg(argc, argv, "--mb", 512);
  if (mb < 1) {
    mb = 1;
  }
  if (mb > 1024) {
    mb = 1024;  // hard safety cap
  }

  const size_t bytes = static_cast<size_t>(mb) * 1024ULL * 1024ULL;
  std::fprintf(stderr, "cache-daemon: allocating %d MB\n", mb);

  char* buf = nullptr;
  try {
    buf = new char[bytes];
  } catch (const std::bad_alloc&) {
    std::fprintf(stderr, "cache-daemon: allocation failed\n");
    return 1;
  }

  // Dirty every page so RSS matches the allocation.
  for (size_t i = 0; i < bytes; i += 4096) {
    buf[i] = static_cast<char>(i & 0xFF);
  }
  if (bytes > 0) {
    buf[bytes - 1] = 1;
  }

  std::fprintf(stderr, "cache-daemon: holding %d MB RSS\n", mb);
  while (g_running.load(std::memory_order_relaxed)) {
    SleepMs(500);
    // Re-touch a few pages so the allocation is not swapped out immediately.
    for (size_t i = 0; i < bytes; i += 2ULL * 1024ULL * 1024ULL) {
      buf[i] = static_cast<char>(buf[i] + 1);
    }
  }

  delete[] buf;
  return 0;
}
