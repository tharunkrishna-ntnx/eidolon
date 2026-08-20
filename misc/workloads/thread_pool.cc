#include "misc_common.h"

#include <pthread.h>
#include <sched.h>

#include <cstdio>
#include <thread>
#include <vector>

static void PinToCpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

int main(int argc, char** argv) {
  InstallSignals();
  const std::string comm = GetStrArg(argc, argv, "--comm", "thread-pool");
  SetComm(comm.c_str());

  int threads = GetIntArg(argc, argv, "--threads", 16);
  const int pin_cpu = GetIntArg(argc, argv, "--pin-cpu", 0);
  const bool pin = !HasFlag(argc, argv, "--no-pin");
  if (threads < 2) {
    threads = 2;
  }
  if (threads > 64) {
    threads = 64;
  }

  std::fprintf(stderr, "thread-pool: threads=%d pin_cpu=%d pin=%s\n", threads,
               pin_cpu, pin ? "yes" : "no");

  std::vector<std::thread> workers;
  workers.reserve(static_cast<size_t>(threads));
  for (int i = 0; i < threads; ++i) {
    workers.emplace_back([pin, pin_cpu] {
      if (pin) {
        PinToCpu(pin_cpu);
      }
      BurnUntil(g_running);
    });
  }
  for (auto& t : workers) {
    t.join();
  }
  return 0;
}
