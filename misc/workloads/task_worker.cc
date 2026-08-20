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
  const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  if (rc != 0) {
    std::fprintf(stderr, "task-worker: failed to pin to CPU %d (rc=%d)\n", cpu,
                 rc);
  }
}

static void PinProcessToCpus(int ncpus) {
  cpu_set_t set;
  CPU_ZERO(&set);
  for (int i = 0; i < ncpus; ++i) {
    CPU_SET(i, &set);
  }
  if (sched_setaffinity(0, sizeof(set), &set) != 0) {
    std::perror("sched_setaffinity");
  }
}

int main(int argc, char** argv) {
  InstallSignals();
  const std::string comm = GetStrArg(argc, argv, "--comm", "task-worker");
  SetComm(comm.c_str());

  const int cpus = GetIntArg(argc, argv, "--cpus", 1);
  const bool pin = HasFlag(argc, argv, "--pin");
  const int n = cpus < 1 ? 1 : cpus;

  std::fprintf(stderr, "task-worker: cpus=%d pin=%s comm=%s\n", n,
               pin ? "yes" : "no", comm.c_str());

  if (pin) {
    // Process-level mask so `taskset -cp PID` matches the worker threads.
    PinProcessToCpus(n);
  }

  std::vector<std::thread> workers;
  workers.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    workers.emplace_back([i, pin] {
      if (pin) {
        PinToCpu(i);
      }
      BurnUntil(g_running);
    });
  }
  for (auto& t : workers) {
    t.join();
  }
  return 0;
}
