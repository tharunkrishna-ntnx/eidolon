# Eidolon Misc Training Scenarios — Facilitator Guide

This document is the facilitator's reference for the local-only *misc*
labs.  These scenarios train everyday tools (`top`, `mpstat`, `pidstat`,
`vmstat`, `ss`/`netstat`, `iostat`) using one-off processes.  They do
**not** use the mock cluster's multi-node machinery.

Each section covers the scenario's purpose, how to set it up, the steps
a trainee should follow, and how to interpret the output.

> **Convention.**  Commands prefixed with `[scorch]` are run as the
> interviewer (master user).  Commands prefixed with `[ember]` are run
> as the trainee (candidate user).  Everything in this guide is
> **local** — there are no `NODE1_IP` / `NODE2_IP` placeholders.

> **Cluster coexistence.**  The mock cluster (atlas, hermes, mnemosyne,
> themis, styx, prometheus) may be running at the same time.  That is
> fine.  Trainees should treat those as baseline services and hunt for
> the *extra* process started by `misc-ctl`.  `misc-ctl scenarios reset`
> stops only the misc workloads; it never restarts or kills cluster
> services.

---

## Prerequisites

One-time setup on the local machine:

```bash
# [scorch] Install sysstat / net-tools and the ember cheatsheet
cd ~/eidolon && bash misc/setup.sh

# [scorch] Build the workload binaries
bash misc/build.sh

# [scorch] Verify tools
top -v | head -1
mpstat -V
pidstat -V
vmstat -V
iostat -V
ss -h | head -1
netstat -V | head -1

# [scorch] Reset misc workloads before each module
./cli/misc-ctl scenarios reset
```

`top`, `free`, `ps`, `vmstat`, and `ss` need **no sudo**.  `mpstat`,
`pidstat`, and `iostat` (sysstat) also work as ember without sudo for
these labs.  `netstat -p` may hide other users' process names without
root; `ss -tlnp` as ember will still show ember-owned sockets, which is
enough.

A copy of `packaging/misc-tools-cheatsheet.txt` is installed to
`~/reference/misc-tools-cheatsheet.txt` for ember by `misc/setup.sh`.

Optional bash completion for the interviewer:

```bash
# [scorch]
source ~/eidolon/packaging/bash_completion_misc_ctl.sh
```

---

## M001 — High CPU process

| Field       | Value                                      |
|-------------|--------------------------------------------|
| Difficulty  | Easy                                       |
| Module      | 1                                          |
| Process     | `task-worker`                              |
| Technique   | `top`, `mpstat`, `taskset`                 |
| Time est.   | 10–15 min                                  |

### Customer complaint (give this to the trainee)

> **Ticket #5101 — Host feels sluggish, CPU looks busy**
>
> Users on this machine report that interactive sessions feel laggy.
> Someone glanced at `uptime` and the load average is higher than they
> expect for an otherwise idle box.  We have not changed cluster
> configs recently.
>
> **Your task:** Identify which process is consuming CPU, record its
> PID and user, and determine whether the work is spread across CPUs or
> concentrated on one core.  Bring evidence from `top` and `mpstat`.

### What is injected

`task-worker` runs a tight arithmetic loop on **one thread pinned to
CPU 0**.  `top` shows ~100% CPU for that PID.  `mpstat -P ALL` shows a
single core near 100% `%usr` while the others stay idle.

### Setup

```bash
# [scorch]
./cli/misc-ctl scenarios apply M001_high_cpu

# sanity check
./cli/misc-ctl status
./cli/misc-ctl scenarios verify M001_high_cpu
```

You should see `task-worker` running as ember with CPU ≥ 80%.

### Solving steps (as ember)

**Step 1 — Find the hot process with `top`.**

```bash
# [ember]
top
# press Shift-P to sort by CPU, then q to quit
```

*What to look for:*  A process named `task-worker` near the top, `%CPU`
around 100 (one full core), USER `ember`.  Note the PID.

```bash
# [ember] non-interactive snapshot
ps -eo pid,user,%cpu,%mem,comm --sort=-%cpu | head -15
```

**Step 2 — See which CPU is busy.**

```bash
# [ember]
mpstat -P ALL 1 5
```

*Expected output:*  CPU 0 (or whichever core was pinned) shows `%usr`
in the 90–100 range.  Other CPUs stay near 0.  The `all` row looks
milder because it is an average.

**Step 3 — Confirm affinity.**

```bash
# [ember]
taskset -cp $(pgrep -x task-worker)
```

*Expected output:*  `current affinity list: 0` (a single CPU).

### How to explain the result

- Load average went up because one task is always runnable.
- `top` answers *which process*; `mpstat` answers *which CPU*.
- Pinning explains the imbalance: this is not a well-spread workload.

### Why this matters in production

A stuck busy-loop (runaway thread, tight poll, broken backoff) often
shows up as one core pegged while the rest of the machine looks fine.
`mpstat` prevents the mistake of looking only at aggregate CPU%.

### Teardown

```bash
# [scorch]
./cli/misc-ctl scenarios reset
```

---

## M002 — High memory process

| Field       | Value                                      |
|-------------|--------------------------------------------|
| Difficulty  | Easy                                       |
| Module      | 1                                          |
| Process     | `cache-daemon`                             |
| Technique   | `top` (Shift-M), `free`, `ps`              |
| Time est.   | 10–15 min                                  |

### Customer complaint (give this to the trainee)

> **Ticket #5102 — Memory usage jumped, box not swapping (yet)**
>
> Monitoring shows available memory dropped by several hundred
> megabytes on this host.  CPU is not interesting.  Nobody has deployed
> a new cluster version.
>
> **Your task:** Find the process holding the extra memory, report PID
> and RSS, and say whether the usage is steady or still growing.

### What is injected

`cache-daemon` allocates **512 MB**, touches every page (so RSS ≈ 512
MB), and holds it.  CPU stays near zero.  This is a large *steady*
allocation, not a leak.

### Setup

```bash
# [scorch]
./cli/misc-ctl scenarios apply M002_high_memory
./cli/misc-ctl scenarios verify M002_high_memory
```

RSS should be at least ~400 MB.

### Solving steps (as ember)

**Step 1 — System memory.**

```bash
# [ember]
free -h
```

*What to look for:*  `used` is elevated; `available` is lower than a
quiet baseline.

**Step 2 — Sort processes by resident memory.**

```bash
# [ember]
top
# press Shift-M to sort by RES

# or:
ps -eo pid,user,%cpu,%mem,rss,comm --sort=-rss | head -15
```

*Expected output:*  `cache-daemon` with RSS around **500000+ KB**
(~512 MB).  Instantaneous CPU in `top` is near zero.  `ps` `%CPU` is a
lifetime average, so it can look a bit high for a few seconds while
pages are first touched, then trends down.

**Step 3 — Confirm it is not growing.**

```bash
# [ember]
pid=$(pgrep -x cache-daemon)
ps -o pid,rss,comm -p "$pid"
sleep 5
ps -o pid,rss,comm -p "$pid"
```

*Expected output:*  RSS stays in the same ballpark across samples.

### How to explain the result

- High RSS + idle CPU = memory resident, not compute.
- Two RSS samples a few seconds apart distinguish a hog from a leak
  (see M003).

### Why this matters in production

Caches, JVM heaps, and misplaced in-memory buffers look exactly like
this: one process quietly owns a large, stable RSS.

### Teardown

```bash
# [scorch]
./cli/misc-ctl scenarios reset
```

---

## M003 — Memory leak

| Field       | Value                                      |
|-------------|--------------------------------------------|
| Difficulty  | Easy                                       |
| Module      | 2                                          |
| Process     | `index-builder`                            |
| Technique   | `top`, `ps`, `pidstat -r`                  |
| Time est.   | 15–20 min                                  |

### Customer complaint (give this to the trainee)

> **Ticket #5103 — Process memory keeps climbing**
>
> A batch-style process was started for indexing.  Over a few minutes
> the host's used memory has been drifting up.  We need to know if this
> is a large cache that will plateau or a leak that will keep going.
>
> **Your task:** Identify the process, show that RSS is *growing over
> time*, and estimate the leak rate in MB/s.  Contrast this with a
> process that simply allocated a big buffer once.

### What is injected

`index-builder` starts at **~32 MB** RSS, then allocates **~8 MB/s**
of anonymous memory and never frees it, up to a **512 MB safety cap**.  After the cap it holds, same
as M002 — so run the lab while it is still climbing (first ~60 s).

### Setup

```bash
# [scorch]
./cli/misc-ctl scenarios apply M003_memory_leak
./cli/misc-ctl scenarios verify M003_memory_leak
```

Verify waits a few seconds and checks that RSS grew.

### Solving steps (as ember)

**Step 1 — Find the process.**

```bash
# [ember]
ps -u ember -o pid,user,rss,comm --sort=-rss
```

*What to look for:*  `index-builder` among ember's processes.  RSS starts
around **32 MB** and is rising.  A system-wide `ps --sort=-rss | head`
may hide it behind cluster services and other users until the leak
has grown.

**Step 2 — Watch RSS.**

```bash
# [ember]
pid=$(pgrep -x index-builder)
ps -o rss= -p "$pid"
sleep 5
ps -o rss= -p "$pid"
```

*Expected output:*  Second sample is roughly **40 MB higher** (8 MB/s ×
5 s).  Units from `ps -o rss=` are kilobytes.

**Step 3 — `pidstat -r`.**

```bash
# [ember]
pidstat -r -p "$(pgrep -x index-builder)" 1 8
```

*What to look for:*  `RSS` column steps up each second.  `minflt/s`
(minor faults) is also elevated as new pages are touched.

### How to explain the result

- A leak is a **time series**, not a single RSS number.
- Rate ≈ ΔRSS / Δt.  Here it is about 8 MB/s until the cap.
- M002 (`cache-daemon`) is the control: large but flat.

### Why this matters in production

Unbounded growth ends in OOM-killer or swap thrash.  Catching a
positive slope early is the whole point of watching `pidstat -r` or
container memory stats over a window.

### Teardown

```bash
# [scorch]
./cli/misc-ctl scenarios reset
```

---

## M004 — netstat / ss socket analysis

| Field       | Value                                      |
|-------------|--------------------------------------------|
| Difficulty  | Easy                                       |
| Module      | 3                                          |
| Process     | `health-probe`                             |
| Technique   | `ss`, `netstat`, `/proc/PID/fd`            |
| Time est.   | 15 min                                     |

### Customer complaint (give this to the trainee)

> **Ticket #5104 — Unexpected listener on the host**
>
> Security scanning flagged a TCP listener on port **19080** bound to
> localhost.  We also see a large number of established connections to
> that port.  Please identify the owning process and how many sockets
> it holds.
>
> **Your task:** Map `127.0.0.1:19080` to a PID and command name, count
> ESTABLISHED connections, and explain that this is userspace holding
> sockets (not a kernel leak).

### What is injected

`health-probe` listens on **127.0.0.1:19080** and opens **40**
localhost client connections to itself, leaving them ESTABLISHED.  The
same process owns the listen fd, the accepted fds, and the client fds.

### Setup

```bash
# [scorch]
./cli/misc-ctl scenarios apply M004_netstat
./cli/misc-ctl scenarios verify M004_netstat
```

Socket count should be well above 40.

### Solving steps (as ember)

**Step 1 — Find the listener.**

```bash
# [ember]
ss -tlnp
# or:
netstat -tlnp 2>/dev/null | grep 19080
```

*Expected output:*  `127.0.0.1:19080` in LISTEN, process
`health-probe`, with a PID.

**Step 2 — Inspect established connections.**

```bash
# [ember]
ss -tn dst :19080 or src :19080
ss -s
```

*What to look for:*  On the order of **40** ESTABLISHED pairs (client
and server sides both show up, so the `ss` line count can be ~80).
All are `127.0.0.1`.

**Step 3 — Confirm from `/proc`.**

```bash
# [ember]
pid=$(pgrep -x health-probe)
ls -l /proc/$pid/fd | grep socket | wc -l
```

*Expected output:*  dozens of socket fds (listen + clients + accepted).

### How to explain the result

- `ss -tlnp` / `netstat -tlnp` answer *who is listening*.
- `-p` maps inode → PID.  As ember you will see *your* sockets;
  cluster services owned by ember also appear — ignore ports 7100–7500.
- Lots of ESTABLISHED localhost sockets is a held-connection pattern,
  common in health checkers, connection pools, and proxy leaks.

### Why this matters in production

Port/PID mapping is the first step in "what is bound to 443?" or "why
are we at fd ulimit?".  `ss` is the modern default; `netstat` is still
widely used in runbooks.

### Teardown

```bash
# [scorch]
./cli/misc-ctl scenarios reset
```

---

## M005 — pidstat: per-process CPU and I/O

| Field       | Value                                      |
|-------------|--------------------------------------------|
| Difficulty  | Easy                                       |
| Module      | 4                                          |
| Process     | `log-shipper`                              |
| Technique   | `pidstat -u/-d/-r`                         |
| Time est.   | 15–20 min                                  |

### Customer complaint (give this to the trainee)

> **Ticket #5105 — Worker is "busy" but we cannot tell CPU vs disk**
>
> A log-shipping style process is active.  `top` shows some CPU.
> Disk LEDs / iostat are not obviously saturated.  We need a
> per-process breakdown of CPU, I/O, and memory — not system-wide
> averages.
>
> **Your task:** Use `pidstat` to report `%CPU`, write throughput
> (`kB_wr/s`), and RSS for the offending PID.

### What is injected

`log-shipper` alternates **~200 ms CPU bursts** with **256 KB append
writes**.  CPU% is moderate (tens of percent).  `pidstat -d` shows
steady `kB_wr/s`.

### Setup

```bash
# [scorch]
./cli/misc-ctl scenarios apply M005_pidstat
./cli/misc-ctl scenarios verify M005_pidstat
```

### Solving steps (as ember)

**Step 1 — Identify the PID.**

```bash
# [ember]
pgrep -a -x log-shipper
```

**Step 2 — CPU.**

```bash
# [ember]
pidstat -u -p "$(pgrep -x log-shipper)" 1 8
```

*Expected output:*  `%CPU` typically in the **20–80** range (bursty).
`%user` dominates.

**Step 3 — Disk.**

```bash
# [ember]
pidstat -d -p "$(pgrep -x log-shipper)" 1 8
```

*Expected output:*  `kB_wr/s` around **1 MB/s** (256 KB per cycle).
`kB_rd/s` near 0.

**Step 4 — Memory, then combined.**

```bash
# [ember]
pidstat -r -p "$(pgrep -x log-shipper)" 1 5
pidstat -u -d -r -p "$(pgrep -x log-shipper)" 1 8
```

*What to look for:*  RSS is small and **stable** (not a leak).  The
combined view is the report you would paste into a ticket.

### How to explain the result

- `top` is a good finder; `pidstat` is the per-PID counters tool.
- `-u` / `-d` / `-r` match CPU, disk, and memory questions.
- Compare with M006: `vmstat` is system-wide; `pidstat` attributes it.

### Why this matters in production

When several services share a node, system-wide CPU/I/O does not tell
you *who*.  `pidstat` is the sysstat answer to that question.

### Teardown

```bash
# [scorch]
./cli/misc-ctl scenarios reset
```

---

## M006 — vmstat: system-wide pressure

| Field       | Value                                      |
|-------------|--------------------------------------------|
| Difficulty  | Medium                                     |
| Module      | 4                                          |
| Process     | `page-scanner`                             |
| Technique   | `vmstat`, then `pidstat` to attribute      |
| Time est.   | 15–20 min                                  |

### Customer complaint (give this to the trainee)

> **Ticket #5106 — Host stats look "busy" in aggregate**
>
> An on-call dashboard shows the node is doing more I/O and some user
> CPU than our quiet baseline.  We do not yet know which process is
> responsible.  Start from **system-wide** tools, then narrow down.
>
> **Your task:** Capture `vmstat 1` output, explain which columns
> moved, then identify the PID that accounts for the `bi`/`bo` and CPU
> activity.

### What is injected

`page-scanner` writes a wrapping **64 MB** file in 256 KB chunks and
**mmap-touches** pages.  `vmstat` should show non-zero `bo` (and
possibly `bi`), some `us`, and `r` occasionally ≥ 1.  Swap `si`/`so`
should stay 0.

### Setup

```bash
# [scorch]
./cli/misc-ctl scenarios apply M006_vmstat
./cli/misc-ctl scenarios verify M006_vmstat
```

### Solving steps (as ember)

**Step 1 — System-wide sample.**

```bash
# [ember]
vmstat 1 10
```

*How to read this:*  **Discard the first line** — it is averages since
boot.  Subsequent lines are 1-second snapshots.

| Column | Meaning | What you should see |
|--------|---------|---------------------|
| `r`    | runnable tasks | often 1+ |
| `us`   | user CPU % | elevated vs idle |
| `bi`/`bo` | blocks in/out | `bo` clearly non-zero |
| `si`/`so` | swap | stay 0 |
| `wa`   | I/O wait | maybe small on SSD |

**Step 2 — Attribute to a process.**

```bash
# [ember]
ps -eo pid,user,%cpu,comm --sort=-%cpu | head -10
pidstat -u -d -p "$(pgrep -x page-scanner)" 1 5
```

*Expected output:*  `page-scanner` shows CPU and `kB_wr/s` that line up
with the `vmstat` `us` / `bo` columns.

### How to explain the result

- `vmstat` is the 10,000-foot view (run queue, CPU split, I/O, swap).
- It cannot name a process — that is why you follow with `pidstat` or
  `top`.
- First-line-is-since-boot is a classic interview trip-up.

### Why this matters in production

`vmstat 1` is the fastest "is this box swapping, I/O-bound, or
run-queue bound?" check.  Pair it with a per-process tool before you
change anything.

### Teardown

```bash
# [scorch]
./cli/misc-ctl scenarios reset
```

---

## M007 — Thread storm

| Field       | Value                                      |
|-------------|--------------------------------------------|
| Difficulty  | Easy                                       |
| Module      | 5                                          |
| Process     | `thread-pool`                              |
| Technique   | `top -H`, `ps -L`, `pidstat -t`            |
| Time est.   | 10–15 min                                  |

### Customer complaint (give this to the trainee)

> **Ticket #5107 — One process, but CPU accounting looks odd**
>
> `top` shows a single process using a core, yet dumps and metrics talk
> about a *thread pool*.  We need to confirm how many threads it has
> and that they share one CPU.
>
> **Your task:** Report PID, thread count, and per-thread CPU.  Explain
> why process-level `%CPU` is not the same as per-thread `%CPU`.

### What is injected

`thread-pool` starts **16** busy-loop threads **all pinned to CPU 0**.
Process `%CPU` is ~100 (one core).  `top -H` shows ~16 TIDs sharing
that core (each a fraction of 100%).  `/proc/PID/task` has ≥ 16
entries.

### Setup

```bash
# [scorch]
./cli/misc-ctl scenarios apply M007_thread_storm
./cli/misc-ctl scenarios verify M007_thread_storm
```

Thread count should be ≥ 16.

### Solving steps (as ember)

**Step 1 — Process view.**

```bash
# [ember]
ps -o pid,nlwp,%cpu,comm -p "$(pgrep -x thread-pool)"
```

*Expected output:*  `NLWP` around **16–17** (workers + main).  `%CPU`
~100.

**Step 2 — Thread view in `top`.**

```bash
# [ember]
top -H -p "$(pgrep -x thread-pool)"
```

*What to look for:*  Many rows with the same PID, different TIDs,
small `%CPU` each, same command name.

**Step 3 — `ps -L` and `pidstat -t`.**

```bash
# [ember]
ps -L -p "$(pgrep -x thread-pool)" -o pid,tid,psr,pcpu,comm
pidstat -t -p "$(pgrep -x thread-pool)" 1 5
```

*Expected output:*  `PSR` (processor) is **0** for the workers.
`pidstat -t` lists TIDs with `%CPU`.

### How to explain the result

- Default `top` is **process** aggregated.  `H` / `-H` is **threads**.
- Many threads on one CPU will not give you 1600% CPU; they contend.

### Why this matters in production

Java, Go, and thread-pool services hide work in TIDs.  Debugging a
hotspot often requires `top -H` or `pidstat -t` before `perf`.

### Teardown

```bash
# [scorch]
./cli/misc-ctl scenarios reset
```

---

## M008 — I/O-bound writer

| Field       | Value                                      |
|-------------|--------------------------------------------|
| Difficulty  | Medium                                     |
| Module      | 5                                          |
| Process     | `page-scanner` (fsync path)                |
| Technique   | `iostat`, `vmstat`, `pidstat -d`           |
| Time est.   | 15–20 min                                  |

### Customer complaint (give this to the trainee)

> **Ticket #5108 — Disk looks busy, CPU does not**
>
> The host is writing more than usual.  CPU is not pegged.  Please
> confirm there is real write I/O (not just a memory hog) and name the
> process doing the writes.
>
> **Your task:** Capture `iostat` and `vmstat` evidence of write I/O,
> then attribute it with `pidstat -d`.  On a fast SSD `%iowait` may
> stay low — still report `wkB/s` / `bo` / `await`.

### What is injected

`page-scanner` writes **1 MB** chunks into a wrapping **128 MB** file
and **fsyncs** when the file wraps, so traffic reaches the disk rather
than dying in page cache.  CPU is modest compared with M001.

### Setup

```bash
# [scorch]
./cli/misc-ctl scenarios apply M008_io_bound
./cli/misc-ctl scenarios verify M008_io_bound
```

### Solving steps (as ember)

**Step 1 — `iostat`.**

```bash
# [ember]
iostat -xz 1 8
```

*How to read this:*  Ignore the first sample.  Look at the data device
(`sda`, `vda`, `nvme0n1`, …): `wkB/s` and `w/s` should be clearly
non-zero.  `await` may be < 1 ms on SSD.  `%util` varies.

**Step 2 — `vmstat`.**

```bash
# [ember]
vmstat 1 8
```

*What to look for:*  `bo` elevated vs idle.  `wa` may bump on slower
disks.

**Step 3 — Attribute.**

```bash
# [ember]
pidstat -d -p "$(pgrep -x page-scanner)" 1 5
```

*Expected output:*  `kB_wr/s` in the high hundreds to thousands.

### How to explain the result

- I/O-bound ≠ high `%CPU`.  Use `iostat` / `vmstat wa` / `pidstat -d`.
- Fast SSDs hide wait time; throughput columns still tell the story.
- Same binary family as M006, different flags (`--fsync`, larger
  writes) so the I/O is heavier.

### Why this matters in production

Logging, compaction, and fsync-heavy databases show up here.  Mixing up
"CPU busy" and "disk busy" leads to the wrong fix.

### Teardown

```bash
# [scorch]
./cli/misc-ctl scenarios reset
```

---

## Quick Reference: Tool Selection Guide

| Symptom                         | First tool              | Follow-up                     |
|---------------------------------|-------------------------|-------------------------------|
| One process hot                 | `top` (Shift-P)         | `mpstat -P ALL`               |
| One core hot, others idle       | `mpstat -P ALL 1`       | `taskset -cp PID`             |
| Large stable RSS                | `top` (Shift-M), `free` | `ps -o rss= -p PID` twice     |
| RSS growing                     | `ps` / `top` over time  | `pidstat -r`                  |
| Unexpected TCP port             | `ss -tlnp`              | `ss -tnp dst :PORT`           |
| Per-process CPU + disk + RSS    | `pidstat -u -d -r`      | `top` to find the PID         |
| System-wide "is it busy?"       | `vmstat 1`              | `pidstat` to attribute        |
| Disk writes / await             | `iostat -xz 1`          | `pidstat -d`                  |
| Many threads, one PID           | `top -H` / `ps -L`      | `pidstat -t`                  |

### misc-ctl cheat sheet (`[scorch]`)

```bash
./cli/misc-ctl scenarios list
./cli/misc-ctl scenarios info M001_high_cpu
./cli/misc-ctl scenarios apply M001_high_cpu
./cli/misc-ctl scenarios hint M001_high_cpu
./cli/misc-ctl scenarios verify M001_high_cpu
./cli/misc-ctl status
./cli/misc-ctl scenarios reset
```

`apply` always resets previous misc workloads first.  `reset` never
signals atlas/hermes/mnemosyne/themis/styx/prometheus/oracle.
