#ifndef EIDOLON_MISC_COMMON_H_
#define EIDOLON_MISC_COMMON_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/prctl.h>
#include <unistd.h>

inline std::atomic<bool> g_running{true};

inline void HandleSignal(int) { g_running.store(false, std::memory_order_relaxed); }

inline void InstallSignals() {
  struct sigaction sa {};
  sa.sa_handler = HandleSignal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
}

inline void SetComm(const char* name) {
  if (name && *name) {
    prctl(PR_SET_NAME, name, 0, 0, 0);
  }
}

inline bool HasFlag(int argc, char** argv, const char* flag) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) {
      return true;
    }
  }
  return false;
}

inline std::string GetStrArg(int argc, char** argv, const char* flag,
                             const char* def) {
  const size_t flen = std::strlen(flag);
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0 && i + 1 < argc) {
      return argv[i + 1];
    }
    if (std::strncmp(argv[i], flag, flen) == 0 && argv[i][flen] == '=') {
      return argv[i] + flen + 1;
    }
  }
  return def ? def : "";
}

inline int GetIntArg(int argc, char** argv, const char* flag, int def) {
  const std::string s = GetStrArg(argc, argv, flag, "");
  if (s.empty()) {
    return def;
  }
  return std::atoi(s.c_str());
}

inline void BurnUntil(std::atomic<bool>& running) {
  volatile uint64_t acc = 1;
  while (running.load(std::memory_order_relaxed)) {
    acc += acc * 1664525ULL + 1013904223ULL;
  }
}

inline void BurnMs(int ms, std::atomic<bool>& running) {
  const auto end =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  volatile uint64_t acc = 1;
  while (running.load(std::memory_order_relaxed) &&
         std::chrono::steady_clock::now() < end) {
    acc += acc * 1664525ULL + 1013904223ULL;
  }
}

inline void SleepMs(int ms) {
  int left = ms;
  while (left > 0 && g_running.load(std::memory_order_relaxed)) {
    const int slice = left > 100 ? 100 : left;
    usleep(static_cast<useconds_t>(slice) * 1000);
    left -= slice;
  }
}

#endif  // EIDOLON_MISC_COMMON_H_
