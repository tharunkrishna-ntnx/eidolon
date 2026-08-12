# Eidolon Training Scenarios — Facilitator Guide

This document is the facilitator's reference for running the six perf/BPF
training modules.  Each section covers the scenario's purpose, how to set it up,
the exact steps a trainee should follow to solve it, and how to interpret the
output at every stage.

> **Convention.**  Commands prefixed with `[scorch]` are run as the interviewer
> (master user).  Commands prefixed with `[ember]` are run as the trainee
> (candidate user).  Replace `NODE1_IP` / `NODE2_IP` with the actual cluster
> member IPs (run `cluster status` as ember to discover them).

---

## Prerequisites

Before running any scenario, ensure the cluster is healthy and tooling is
installed.

```bash
# [scorch] Install perf/BPF tools on all nodes (one-time)
cd ~/eidolon && bash tools/setup-training.sh

# [scorch] Verify tools on any node
sudo perf --version        # should print perf version 5.x+
bpftrace --version         # should print bpftrace v0.14+
strace --version           # should print strace 5.x+

# [scorch] Ensure kernel allows profiling (required once per boot)
sudo sysctl -w kernel.perf_event_paranoid=1
sudo sysctl -w kernel.kptr_restrict=0
sudo sysctl -w kernel.yama.ptrace_scope=0

# [scorch] Reset to a clean baseline before each module
./cli/eidolon-ctl scenarios reset
```

### Tool permissions for the ember user

The ember user has **limited `sudo` access** — only performance and tracing
tools are allowed (passwordless, via a sudoers drop-in at
`/etc/sudoers.d/eidolon-training`).  All other `sudo` commands are denied.

| Tool | sudo needed? | Notes |
|------|-------------|-------|
| `perf stat/record/report/top/script` | Optional — works without sudo | `perf_event_paranoid=1`; `sudo` also allowed as fallback |
| `perf trace` | Yes — sudo required | Needs `/sys/kernel/tracing`; allowed via sudoers |
| `strace`, `strace -f -c` | Optional — works without sudo | `ptrace_scope=0`; `sudo` also allowed as fallback |
| `bpftrace` | Yes — sudo required | BPF program loading requires root |
| `biolatency-bpfcc` | Yes — sudo required | BCC tools require root |
| `offcputime-bpfcc` | Yes — sudo required | BCC tools require root |
| `tcpconnect-bpfcc` | Yes — sudo required | BCC tools require root |
| `opensnoop-bpfcc` | Yes — sudo required | BCC tools require root |
| Any other `sudo` command | **Denied** | Not in the allow-list |

The kernel sysctl settings (`perf_event_paranoid`, `ptrace_scope`) are
persisted in `/etc/sysctl.d/99-eidolon.conf` so they survive reboots.  The
sudoers drop-in acts as an additional safety net — even if the sysctl defaults
are restored, ember can still `sudo perf ...` and `sudo strace ...`.

**Automatic setup:** `deploy.sh` installs both the sudoers drop-in and sysctl
config on every node automatically.  To install or update manually on a single
node:

```bash
# [scorch] Install limited sudo for ember
echo 'ember ALL=(root) NOPASSWD: /usr/bin/perf *, /usr/bin/perf, \
    /usr/bin/strace *, /usr/bin/strace, \
    /usr/bin/bpftrace *, /usr/bin/bpftrace, \
    /usr/sbin/biolatency-bpfcc *, /usr/sbin/biolatency-bpfcc, \
    /usr/sbin/offcputime-bpfcc *, /usr/sbin/offcputime-bpfcc, \
    /usr/sbin/tcpconnect-bpfcc *, /usr/sbin/tcpconnect-bpfcc, \
    /usr/sbin/opensnoop-bpfcc *, /usr/sbin/opensnoop-bpfcc' \
  | sudo tee /etc/sudoers.d/eidolon-training > /dev/null
sudo chmod 0440 /etc/sudoers.d/eidolon-training
```

All `[ember]` commands in this document are written **without** `sudo` for
tools that work as a regular user.  Commands that genuinely require root are
prefixed with `sudo` and noted accordingly.

---

## T101 — CPU-Bound vs Memory-Bound Analysis

| Field       | Value                              |
|-------------|------------------------------------|
| Difficulty  | Easy                               |
| Module      | 1                                  |
| Service     | Atlas (VM lifecycle manager)       |
| Technique   | `perf stat`, `perf record`, flamegraphs |
| Time est.   | 20–30 min                          |

### Customer complaint (give this to the trainee)

> **Ticket #4271 — VM placement inconsistencies across cluster**
>
> We have a 3-node cluster running Atlas for VM lifecycle management. The
> customer reports that VM placement operations are noticeably slower on one
> node compared to the others, and a second node appears to be consuming an
> unusual amount of memory. The customer wants to understand what is different
> about these two nodes and whether there is a configuration issue.
>
> **Your task:** SSH into both affected nodes, profile the Atlas service on
> each, and produce a comparison explaining why the two nodes behave
> differently. Include evidence from profiling tools to support your analysis.

### What is injected

Atlas on **Node 1** is set to an extreme scheduling depth (10 000 iterations per
cycle) with a small 4 MB table — this creates a **CPU-bound** workload where the
scheduler burns through arithmetic in a tight loop.

Atlas on **Node 2** is set to a huge 512 MB VM table with 500 000 random lookups
per cycle — this creates a **memory-bound** workload where most time is spent
chasing cache misses across a large data structure.

### Setup

```bash
# [scorch]
./cli/eidolon-ctl scenarios apply T101_cpu_vs_memory
```

Verify both Atlas instances are running:

```bash
# [scorch] — quick sanity check
ssh scorch@NODE1_IP 'ps -p $(pgrep -x atlas) -o %cpu,%mem,rss'
ssh scorch@NODE2_IP 'ps -p $(pgrep -x atlas) -o %cpu,%mem,rss'
```

You should see Node 2 using ~500+ MB RSS and higher CPU than Node 1.

### Solving steps (as ember)

**Step 1 — Observe resource usage.**

```bash
# [ember] SSH to each node and check Atlas resources
ps -p $(pgrep -x atlas) -o pid,%cpu,%mem,rss
```

*What to look for:*  Node 1 will show moderate CPU (~5–10%) with low memory
(~0.1%, a few MB).  Node 2 will show high CPU (~50–60%) and high RSS (~530 MB).
This immediately tells you the workloads are qualitatively different.

**Step 2 — Profile with `perf stat`.**

```bash
# [ember] On each node
perf stat -e task-clock,context-switches,page-faults \
  -p $(pidof atlas) -- sleep 10
```

*What to look for:*  Compare `task-clock` values.  Node 2's value will be ~10x
Node 1's — it is spending far more CPU time per wall-clock second.  Context
switches should be similar, ruling out I/O contention.

> **Note:** Hardware PMU counters (cycles, instructions, cache-misses) are not
> available in nested virtualization.  On bare metal you would compare IPC
> directly — low IPC = memory-bound.  In this environment we use function-level
> profiling instead.

**Step 3 — Identify hot functions with `perf record`.**

```bash
# [ember] On Node 1 (CPU-bound)
perf record -F 99 -p $(pidof atlas) -g --call-graph dwarf -- sleep 30
perf report --stdio --no-children | head -40
```

*Expected output on Node 1:*

```
    50.00%  atlas  atlas  [.] eidolon::atlas::Scheduler::RunCycle
    25.00%  atlas  atlas  [.] eidolon::atlas::VmTable::UnplacedCount
```

`RunCycle` (which internally calls `ScorePlacement`) dominates — the CPU is busy
doing scheduling math in a tight loop.

```bash
# [ember] On Node 2 (memory-bound)
perf record -F 99 -p $(pidof atlas) -g --call-graph dwarf -- sleep 30
perf report --stdio --no-children | head -40
```

*Expected output on Node 2:*

```
    68.59%  atlas  atlas  [.] eidolon::atlas::VmTable::UnplacedCount
    28.88%  atlas  atlas  [.] eidolon::atlas::VmTable::RandomLookup
```

`RandomLookup` and `UnplacedCount` dominate — the CPU is stalled on memory
accesses across a 512 MB data structure.

**Step 4 — Generate flamegraphs (optional).**

```bash
# [ember] If FlameGraph scripts are installed in ~/bin/
perf script | ~/bin/stackcollapse-perf.pl | ~/bin/flamegraph.pl > atlas.svg
```

The flamegraphs will show visually distinct towers: scheduling functions on
Node 1, table-scan functions on Node 2.

### How to explain the result

- **CPU-bound** means the processor is fully utilized doing useful work
  (arithmetic, comparisons).  IPC is typically high.  The bottleneck is raw
  compute throughput.
- **Memory-bound** means the processor spends most of its cycles waiting for
  data to arrive from RAM/cache.  IPC is typically low.  The bottleneck is
  memory bandwidth and latency.
- The trainee should conclude: "Node 1 is CPU-bound because its hot function is
  a computation loop.  Node 2 is memory-bound because its hot function does
  random accesses across a 512 MB table."

### Teardown

```bash
# [scorch]
./cli/eidolon-ctl scenarios reset
```

---

## T102 — Flamegraph CPU Hotspot Investigation

| Field       | Value                              |
|-------------|------------------------------------|
| Difficulty  | Easy                               |
| Module      | 2                                  |
| Service     | Hermes (I/O service)               |
| Technique   | `perf record`, `perf report`, flamegraphs |
| Time est.   | 15–20 min                          |

### Customer complaint (give this to the trainee)

> **Ticket #4305 — Hermes I/O throughput degraded after config change**
>
> After a recent maintenance window, the customer is reporting that Hermes
> write throughput has dropped significantly. They say I/O latencies have gone
> up and CPU usage on the Hermes process is much higher than it was before the
> change. No new workloads have been added — the same set of VMs is running
> the same applications.
>
> **Your task:** Investigate what is consuming CPU inside the Hermes process.
> Produce a flamegraph showing where time is being spent and identify the
> function responsible for the regression.

### What is injected

Hermes has `inline_integrity_check` enabled.  Every write operation passes the
data through a deliberately expensive CRC32c computation (compiled with
`-O0` to prevent optimization).  Under the `sequential_heavy` Oracle profile
(which sends large 1 MB blocks), this function consumes ~99% of CPU.

### Setup

```bash
# [scorch]
./cli/eidolon-ctl scenarios apply T102_flamegraph_hotspot
```

Wait ~10 seconds for Oracle to start sending traffic, then verify Hermes is
under load:

```bash
# [scorch]
ps -p $(pgrep -x hermes) -o %cpu
# Should show 20–30%+ CPU
```

### Solving steps (as ember)

**Step 1 — Live hot-function view.**

```bash
# [ember]
perf top -p $(pidof hermes)
```

*What to look for:*  `eidolon::hermes::ComputeBlockIntegrity` should
immediately appear near the top.  This gives an instant signal that one function
is dominating.

**Step 2 — Capture a profile.**

```bash
# [ember]
perf record -F 999 -p $(pidof hermes) -g --call-graph dwarf -- sleep 30
```

We use `-F 999` (999 Hz sampling) for better resolution.  The `--call-graph
dwarf` flag captures full stack traces using DWARF debug info.

**Step 3 — View the report.**

```bash
# [ember]
perf report --stdio --no-children
```

*Expected output:*

```
    99.17%  hermes   hermes  [.] eidolon::hermes::ComputeBlockIntegrity
            |
            ---eidolon::hermes::ComputeBlockIntegrity
               eidolon::hermes::HermesService::ProcessCommand
```

A single function consumes 99% of CPU.  The call chain shows it is invoked from
`ProcessCommand` — i.e., on every incoming I/O request.

**Step 4 — Generate a flamegraph.**

```bash
# [ember]
perf script | ~/bin/stackcollapse-perf.pl | ~/bin/flamegraph.pl > hermes.svg
```

The SVG will show a single massive tower for `ComputeBlockIntegrity`.  This is
the textbook example of what a CPU hotspot looks like in a flamegraph.

### How to explain the result

- Flamegraphs visualize stack traces sampled over time.  The **width** of a
  function's bar is proportional to the fraction of samples where that function
  was on-CPU.
- A single tower consuming nearly 100% means every sample landed in the same
  function — it is the undisputed bottleneck.
- The trainee should trace the call chain: the hot function is called from the
  write path, suggesting it is a per-I/O computation.  Checking the gflags file
  (`/home/ember/config/hermes.gflags`) reveals `inline_integrity_check=True`.

### Why this matters in production

CRC/checksum bugs are a real category of performance issue.  Accidental double-
checksumming, or checksumming at the wrong layer, can silently consume all
available CPU.  Flamegraphs make these issues immediately visible.

### Teardown

```bash
# [scorch]
./cli/eidolon-ctl scenarios reset
```

---

## T103 — The Idle-but-Slow Mystery

| Field       | Value                              |
|-------------|------------------------------------|
| Difficulty  | Medium                             |
| Module      | 3                                  |
| Service     | Mnemosyne (metadata store)         |
| Technique   | `strace`, syscall analysis         |
| Time est.   | 20–30 min                          |

### Customer complaint (give this to the trainee)

> **Ticket #4412 — Metadata operations sluggish, but nothing looks busy**
>
> The customer reports that metadata lookups and writes in Mnemosyne feel
> "laggy" — operations that used to return instantly now take hundreds of
> milliseconds. However, when they check `top`, Mnemosyne is barely using any
> CPU. The cluster has plenty of free memory and disk space. They ran `perf
> top` on the process and it showed almost nothing.
>
> **Your task:** Determine why Mnemosyne is slow despite appearing idle.
> Standard CPU profiling has already been ruled out — you'll need to look
> elsewhere. Write a one-paragraph root cause explaining what is happening
> and why CPU profiling missed it.

### What is injected

Mnemosyne has `validate_metadata_paths` enabled.  On every PUT operation, it
calls `stat()` 500 times on nonexistent schema paths (`schema_v0.meta` through
`schema_v499.meta`).  These all return `ENOENT`.  The process appears idle in
`top` (low CPU) but is extremely slow to respond because each PUT triggers 500
wasted kernel round-trips.

### Setup

```bash
# [scorch]
./cli/eidolon-ctl scenarios apply T103_idle_but_slow
```

### Solving steps (as ember)

**Step 1 — Confirm the process is "idle".**

```bash
# [ember]
perf top -p $(pidof mnemosyne)
# Very little activity; mostly clock_nanosleep
```

This is the trap: CPU profiling shows nothing.  A common beginner mistake is to
stop here and declare "it's fine."

**Step 2 — Switch to syscall tracing.**

When CPU profilers come up empty but the service is slow, the next tool is
`strace`.  The `-f` flag is critical because Mnemosyne handles each client in a
separate thread.

```bash
# [ember] — while traffic is flowing (Oracle sends steady writes via Hermes)
strace -f -c -p $(pidof mnemosyne)
# Let it run for ~15 seconds, then Ctrl-C
```

*Expected output:*

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 57.40    1.891331       12779       148           clock_nanosleep
 28.11    0.926283       18162        51           accept
 13.59    0.447718          17     25101     25000 newfstatat
```

*How to read this:*

- `newfstatat` (the kernel implementation of `stat()`) was called **25,101**
  times with **25,000 errors**.  The error count is the giveaway — nearly
  every call fails.
- The 25,000 count corresponds to ~50 PUTs x 500 stat() calls each.
- Even though each individual `stat()` is fast (~17 microseconds), 500 of them
  per operation add up to ~8.5 ms of latency per PUT — enough to make the
  service feel sluggish.

**Step 3 — Dig deeper with individual traces.**

```bash
# [ember] — see actual file paths being stat'd
strace -f -e trace=newfstatat -p $(pidof mnemosyne) 2>&1 | head -20
```

*Expected output:*

```
[pid 12345] newfstatat(AT_FDCWD, "/home/ember/data/mnemosyne/schema_v0.meta", ...) = -1 ENOENT
[pid 12345] newfstatat(AT_FDCWD, "/home/ember/data/mnemosyne/schema_v1.meta", ...) = -1 ENOENT
...
```

The pattern becomes obvious: a loop is stat'ing sequential schema version files
that don't exist.

### How to explain the result

- This is the classic **"idle but slow"** pattern.  The CPU is not the
  bottleneck — the process spends most of its time in very fast but very
  numerous kernel calls.
- `perf top` won't catch this because each `stat()` is so fast it barely
  registers as CPU time.  But the cumulative effect on latency is significant.
- The root cause is the `validate_metadata_paths` gflag, which triggers a
  validation loop on every PUT that stat's 500 nonexistent paths.

### Why this matters in production

This pattern appears in real systems when configuration validation or schema
migration checks accidentally run on the hot path instead of at startup.  Tools
like `strace -c` are indispensable for catching these "death by a thousand
cuts" issues.

### Teardown

```bash
# [scorch]
./cli/eidolon-ctl scenarios reset
```

---

## T104 — Off-CPU Stall: Disk Sync Bottleneck

| Field       | Value                              |
|-------------|------------------------------------|
| Difficulty  | Medium                             |
| Module      | 3                                  |
| Service     | Mnemosyne (metadata store)         |
| Technique   | `strace`, off-CPU analysis         |
| Time est.   | 20–30 min                          |

### Customer complaint (give this to the trainee)

> **Ticket #4438 — Mnemosyne write latency spikes under normal load**
>
> The customer has noticed that Mnemosyne metadata writes are taking 5–10x
> longer than expected. The issue is consistent — every single write is slow,
> not just occasional spikes. CPU usage on Mnemosyne is negligible and there
> are no memory pressure warnings. The customer suspects a disk issue but
> `iostat` shows the drive isn't saturated.
>
> **Your task:** Figure out where Mnemosyne is spending its time if not on
> CPU. Identify the specific system call that is blocking the process and
> explain why it causes latency despite low CPU usage. Provide disk I/O
> evidence to support your findings.

### What is injected

Mnemosyne has `sync_on_write` enabled.  After every PUT, it opens the data
directory and calls `fsync()` to force a durable write.  This blocks the handler
thread until the disk completes the I/O, adding milliseconds of latency per
operation.

### Setup

```bash
# [scorch]
./cli/eidolon-ctl scenarios apply T104_off_cpu_stall
```

### Solving steps (as ember)

**Step 1 — Confirm low CPU, slow throughput.**

```bash
# [ember]
ps -p $(pidof mnemosyne) -o %cpu,%mem
# CPU should be very low
```

**Step 2 — Syscall summary.**

```bash
# [ember] — while traffic is flowing
strace -f -c -p $(pidof mnemosyne)
# Run for ~15 seconds, Ctrl-C
```

*Expected output:*

```
% time     seconds  usecs/call     calls    errors syscall
...
  1.17    0.004179          83        50           fsync
...
  0.44    0.001558          31        50           openat
```

*How to read this:*  Every PUT generates one `openat` + one `fsync` pair.  The
`fsync` calls are the bottleneck — they force the kernel to flush dirty pages
to the physical disk, blocking the thread until the write is durable.

**Step 3 — Trace individual slow syscalls.**

```bash
# [ember] — use strace with per-call timing
strace -f -T -e trace=fsync -p $(pidof mnemosyne) 2>&1 | head -20
```

The `-T` flag prints the time spent in each syscall.  You should see `fsync`
calls with durations in the hundreds-of-microseconds to low-millisecond range.

**Step 4 — Block I/O latency histogram (requires root).**

> These commands require root.  The facilitator can run them or grant ember
> temporary access (see Prerequisites).

```bash
# [scorch or root shell]
sudo biolatency-bpfcc
# Or with bpftrace:
sudo bpftrace -e '
  tracepoint:block:block_rq_issue { @s[args->sector] = nsecs; }
  tracepoint:block:block_rq_complete /@s[args->sector]/ {
    @us = hist((nsecs - @s[args->sector]) / 1000);
    delete(@s[args->sector]);
  }'
```

This shows the distribution of disk I/O latency across the system.  If fsync
is the bottleneck, you'll see a cluster of completions in the hundreds-of-
microseconds to low-millisecond range.

**Step 5 — Off-CPU analysis (advanced, requires root).**

```bash
# [scorch or root shell]
sudo offcputime-bpfcc -p $(pidof mnemosyne) 10
```

This shows kernel stack traces where the process was sleeping (off-CPU).  Look
for stacks containing `fsync` -> `vfs_fsync` -> `ext4_sync_file` -> I/O
scheduler.

### How to explain the result

- **Off-CPU** problems are the complement of on-CPU problems.  The process isn't
  burning CPU — it's sleeping, waiting for an external resource (disk, network,
  lock).
- `fsync` is an intentional durability mechanism, but calling it on every
  individual write is extremely expensive.  The correct pattern is to batch
  writes and fsync periodically (e.g., once per second), or use a write-ahead
  log.
- The root cause is the `sync_on_write` gflag forcing per-PUT durability.

### Why this matters in production

Accidental per-operation fsyncs are a notorious performance killer.  They convert
what should be buffered I/O (microseconds) into synchronous disk I/O
(milliseconds).  Tools like `strace -f -c` and `biolatency` are the standard
approach for diagnosing these.

### Teardown

```bash
# [scorch]
./cli/eidolon-ctl scenarios reset
```

---

## T105 — Argument Tracing and Latency Histograms

| Field       | Value                              |
|-------------|------------------------------------|
| Difficulty  | Medium                             |
| Module      | 4                                  |
| Service     | Hermes (I/O service)               |
| Technique   | `bpftrace` tracepoints, uprobes, histograms |
| Time est.   | 30–40 min                          |

> **Note:** All exercises in this scenario use `bpftrace`, which requires root.
> The facilitator must grant ember access before starting (see Prerequisites).
> All `bpftrace` commands below are prefixed with `sudo` accordingly.

### Customer complaint (give this to the trainee)

> **Ticket #4500 — Baseline I/O characterization requested**
>
> The SRE team is onboarding a new cluster and wants a baseline profile of
> Hermes I/O behavior under normal load before any production workloads are
> moved over. They need to know: what files does Hermes access, what are the
> typical read/write sizes, and what does the I/O latency distribution look
> like? They also want to know if it's possible to trace individual Hermes
> functions without recompiling the binary.
>
> **Your task:** Using `bpftrace`, produce the following deliverables:
> (1) a trace of file paths Hermes accesses, (2) a histogram of write sizes,
> (3) a latency histogram for read syscalls, and (4) a list of available
> function-level probe points in the Hermes binary. Explain what each output
> tells you about the I/O pattern.

### What is injected

Nothing — the cluster runs normally under steady Oracle load.  This is a pure
skills exercise for learning `bpftrace`.

### Setup

```bash
# [scorch]
./cli/eidolon-ctl scenarios apply T105_argument_tracing
```

### Solving steps (as ember)

**Exercise 1 — Trace file opens.**

```bash
# [ember]
HPID=$(pidof hermes)
sudo bpftrace -e \
  "tracepoint:syscalls:sys_enter_openat /pid == $HPID/ {
    printf(\"%s -> %s\n\", comm, str(args->filename));
  }"
# Run for a few seconds, Ctrl-C
```

*What to look for:*  A stream of block file paths like
`/home/ember/data/hermes/blocks/blk_86ed31689040`.  This shows the actual files
Hermes opens for each I/O operation.

*Why this is useful:*  `openat` tracing reveals the filesystem access pattern
without modifying the application.  In production, you'd use this to detect
unexpected file accesses (e.g., config reloads, log rotation interfering with
I/O).

**Exercise 2 — Write-size histogram.**

```bash
# [ember]
sudo bpftrace -e \
  "tracepoint:syscalls:sys_enter_write /pid == $HPID/ {
    @bytes = hist(args->count);
  }"
# Run for 5 seconds, Ctrl-C
```

*Expected output:*

```
@bytes:
[64, 128)            207 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                       |
[128, 256)           360 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
```

*How to read this:*  bpftrace's `hist()` produces power-of-2 histogram buckets.
Each `@` represents a proportional count.  Here we see most writes are 64–256
bytes (metadata + small block data).

**Exercise 3 — Read latency histogram.**

```bash
# [ember]
sudo bpftrace -e '
  tracepoint:syscalls:sys_enter_read /pid == '"$HPID"'/ { @start[tid] = nsecs; }
  tracepoint:syscalls:sys_exit_read /@start[tid]/ {
    @us = hist((nsecs - @start[tid]) / 1000);
    delete(@start[tid]);
  }'
# Run for 5 seconds, Ctrl-C
```

*Expected output:*

```
@us:
[16, 32)             151 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[32, 64)             122 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@          |
[64, 128)              5 |@                                                   |
...
[32K, 64K)             1 |                                                    |
```

*How to read this:*  Most reads complete in 16–64 microseconds (buffered
filesystem reads).  The rare outlier at 32–64 ms likely represents a read that
triggered actual disk I/O.  This bimodal distribution is typical of buffered
I/O systems.

*How the probe works:*  We record the timestamp at syscall entry (`sys_enter_read`)
keyed by thread ID, then compute the delta at syscall exit (`sys_exit_read`).
The `delete(@start[tid])` prevents stale entries from accumulating.

**Exercise 4 — Discover uprobe targets.**

```bash
# [ember]
sudo bpftrace -l 'uprobe:/home/ember/bin/hermes:*Write*'
```

*Expected output:*

```
uprobe:/home/ember/bin/hermes:_ZN7eidolon6hermes9IoHandler10WriteBlockE...
```

This lists all functions in the Hermes binary matching `*Write*`.  The mangled
C++ names can be demangled with `c++filt`.  You can attach uprobes to trace
user-space function arguments and return values — a powerful technique for
application-level observability without modifying the source.

### How to explain the results

- `bpftrace` attaches eBPF programs to kernel tracepoints and user-space probes
  with negligible overhead.
- Tracepoints (`tracepoint:syscalls:*`) are stable kernel interfaces for
  observing syscall arguments.
- Histograms (`hist()`, `lhist()`) are the preferred way to summarize latency
  distributions — they reveal bimodal distributions that averages would hide.
- Uprobes let you trace any function in an unmodified binary, including its
  arguments via register inspection.

### Teardown

```bash
# [scorch]
./cli/eidolon-ctl scenarios reset
```

---

## T106 — Capstone: Multi-Layer Performance Investigation

| Field       | Value                              |
|-------------|------------------------------------|
| Difficulty  | Hard                               |
| Module      | 5                                  |
| Services    | Hermes, Mnemosyne, Atlas           |
| Technique   | All of the above + kprobes, VFS tracing |
| Time est.   | 45–60 min                          |

> **Note:** Phases 2 and 5 use `bpftrace` (requires root).  Phases 1, 3, and
> 4 use only `ps`, `strace`, and `nc`, which work as ember without `sudo`.

### Customer complaint (give this to the trainee)

> **Ticket #4587 — Cluster-wide performance degradation, multiple services**
>
> A production cluster has started exhibiting widespread performance issues
> following a scheduled configuration update. The customer reports:
>
> - VM I/O writes through Hermes are noticeably slower — applications inside
>   guest VMs are seeing higher-than-normal write latencies.
> - Metadata operations via Mnemosyne have become sluggish — services that
>   query metadata are timing out intermittently.
> - Atlas health monitoring is reporting intermittent failures for services
>   that were previously healthy.
>
> The customer's SRE team has confirmed that no hardware faults are present,
> network connectivity is fine, and no new workloads have been added. They
> believe something in the configuration change introduced one or more
> regressions but they can't pinpoint which settings are at fault.
>
> **Your task:** Perform a full investigation across all affected services.
> For each issue you find, document: (1) the symptom, (2) which tool you used
> to diagnose it, (3) the evidence you collected, and (4) the root cause
> configuration. Produce a runbook-style incident report.

### What is injected

Three faults are active simultaneously:

1. **Hermes** — `io_ack_delay_us=5000`: a 5-millisecond `usleep()` is injected
   after every write acknowledgment, simulating network-level write durability
   latency.
2. **Mnemosyne** — `sync_on_write=true`: every metadata PUT triggers an
   `fsync()`, adding disk latency.
3. **Atlas** — `health_check_targets=hermes,mnemosyne,themis` with a 2-second
   scheduling interval: Atlas is health-checking all services including the
   degraded ones, causing elevated failure counts.

### Setup

```bash
# [scorch]
./cli/eidolon-ctl scenarios apply T106_capstone
```

This takes ~60–90 seconds as it restarts three services across all nodes.

### Solving steps (as ember)

The trainee should work through a systematic top-down investigation.

**Phase 1 — Survey the landscape.**

```bash
# [ember]
cluster status
# Check overall cluster health; services should be up but sluggish

ps aux | grep -E '(hermes|mnemosyne|atlas)' | grep -v grep
# Note CPU usage for each service
```

**Phase 2 — Investigate Hermes (Fault 1).**

The most direct approach uses `strace` to see what Hermes threads are doing:

```bash
# [ember] — Syscall summary with per-call timing
strace -f -c -p $(pidof hermes)
# Run for 10 seconds, Ctrl-C
```

Look for a high count of `clock_nanosleep` calls.  To distinguish the injected
5 ms sleeps from normal idle sleeps, trace individual calls:

```bash
# [ember] — See actual sleep durations
strace -f -T -e trace=clock_nanosleep -p $(pidof hermes) 2>&1 | head -30
```

*What to look for:*  A mix of ~5 ms sleeps (the injected delay) and ~200 ms
sleeps (normal idle loops).  The 5 ms calls will vastly outnumber the 200 ms
calls.

*Alternative with bpftrace (requires root):*

```bash
# [ember] (with sudo access to bpftrace)
HPID=$(pidof hermes)
sudo bpftrace -e \
  "tracepoint:syscalls:sys_enter_clock_nanosleep /pid == $HPID/ {
    @sec[args->rqtp->tv_sec, args->rqtp->tv_nsec] = count();
  }"
# Run for 5 seconds, Ctrl-C
```

*Expected output:*

```
@sec[0, 5000000]: 545
@sec[0, 200000000]: 22
```

Two distinct sleep durations: **5,000,000 ns (5 ms)** called 545 times is the
injected write-ack delay, vs **200,000,000 ns (200 ms)** called 22 times for
normal idle sleeps.  Checking `hermes.gflags` reveals `io_ack_delay_us=5000`.

**Phase 3 — Investigate Mnemosyne (Fault 2).**

```bash
# [ember] — Syscall summary while traffic is flowing
MPID=$(pidof mnemosyne)
strace -f -c -p $MPID
# Run for 15 seconds, Ctrl-C
```

*Expected output:*

```
  0.99    0.002209          73        30           fsync
  0.43    0.000958          31        30           openat
```

*How to read this:*  `fsync` appears with a count matching the number of PUTs.
Each PUT opens the data directory and fsyncs it — the `sync_on_write` flag is
the cause.

**Phase 4 — Investigate Atlas (Fault 3).**

```bash
# [ember]
echo "STATS" | nc -q1 127.0.0.1 7500
```

*Expected output:*

```json
{"total_vms":43690,"unplaced_vms":8378,...,"health_checks_ok":72,"health_checks_fail":36,...}
```

*How to read this:*  A significant fraction of health checks are failing.
This is because Atlas is health-checking Hermes and Mnemosyne, which are
responding slowly due to their own injected faults.  Checking `atlas.gflags`
reveals `health_check_targets=hermes,mnemosyne,themis`.

**Phase 5 — Kernel-layer tracing (advanced, requires root).**

```bash
# [ember] (with sudo access to bpftrace) — VFS write latency histogram
sudo bpftrace -e '
  kprobe:vfs_write { @s[tid] = nsecs; }
  kretprobe:vfs_write /@s[tid]/ {
    @ns = hist(nsecs - @s[tid]);
    delete(@s[tid]);
  }'
# Run for 5 seconds, Ctrl-C
```

*Expected output:*

```
@ns:
[1K, 2K)               98 |@@@@@@                                              |
[2K, 4K)              134 |@@@@@@@@                                            |
...
[16K, 32K)            779 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
```

This shows VFS write latency across all processes, providing a system-wide view
of disk write performance.

**Phase 6 — Write the incident report.**

The deliverable is a runbook-style writeup.  Each fault should be documented:

| # | Symptom | Tool | Evidence | Root Cause |
|---|---------|------|----------|------------|
| 1 | Hermes write latency elevated | `strace -f -c` / `bpftrace` | High count of 5 ms nanosleeps | `io_ack_delay_us=5000` in hermes.gflags |
| 2 | Mnemosyne PUTs slow, low CPU | `strace -f -c` | `fsync` after every PUT | `sync_on_write=true` in mnemosyne.gflags |
| 3 | Atlas health check failures | `nc` STATS endpoint | `health_checks_fail` count rising | `health_check_targets` includes degraded services |

### How to explain the result

The capstone scenario teaches the most important lesson: **production incidents
rarely have a single root cause**.  A systematic approach is required:

1. **Start wide** — use `ps`, `top`, `cluster status` to survey all services.
2. **Go service by service** — isolate each service and determine if it has
   elevated CPU (use `perf top`) or elevated latency with low CPU (use `strace`).
3. **Match the tool to the symptom:**
   - High CPU → `perf record` / flamegraph
   - Low CPU + slow → `strace -f -c` / `strace -f -T`
   - Disk latency → `biolatency` / block tracepoints *(root)*
   - Argument tracing → `bpftrace` with tracepoints or uprobes *(root)*
4. **Correlate across services** — Atlas's health check failures are a
   *downstream symptom* of Hermes and Mnemosyne being slow.  Fixing the
   upstream issues resolves the downstream ones.

### Teardown

```bash
# [scorch]
./cli/eidolon-ctl scenarios reset
```

---

## Quick Reference: Tool Selection Guide

| Symptom                              | First Tool (no root)    | Second Tool (root required)    |
|--------------------------------------|-------------------------|--------------------------------|
| High CPU, one process                | `perf top -p PID`       | `perf record` + flamegraph     |
| Low CPU but slow responses           | `strace -f -c -p PID`  | `bpftrace` tracepoint tracing  |
| Disk I/O suspected                   | `strace -f -T -e fsync` | `biolatency-bpfcc` *(root)*   |
| Need to trace syscall arguments      | `strace -f -e trace=...`| `bpftrace` tracepoints *(root)*|
| Need to trace function calls         | `perf record` + report  | `bpftrace` uprobes *(root)*   |
| Need latency distribution            | `strace -f -T`          | `bpftrace` `hist()` *(root)*  |
| Process blocked/sleeping             | `strace -f -c` (fsync?) | `offcputime-bpfcc` *(root)*   |
| Multiple services affected           | Start wide with `ps`    | Triage each individually       |
