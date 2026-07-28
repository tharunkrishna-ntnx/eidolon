# Eidolon

*In Greek mythology, an eidolon is an astral double, phantom, or an exact, illusory copy of a real person or object, specifically created to deceive, distract, or test others.*

Eidolon is a mock distributed storage cluster environment designed for interviewing DevEx candidates. It provides a simplified but realistic simulation of a multi-node cluster with data services, service lifecycle management, and troubleshooting scenarios.

## Architecture

Eidolon runs on N Ubuntu VMs (typically 3+) with two users per node:

- **Master user** (default: `scorch`) -- interviewer side: source code, build tools, I/O orchestrator, scenario control
- **Candidate user** (default: `ember`) -- candidate side: deployed services, configs, logs, CLI tools

### Services

| Service | Language | Role | Equivalent |
|---------|----------|------|------------|
| **Styx** | Python | Config coordinator, node discovery, leader election | ZooKeeper |
| **Prometheus** | Python | Service lifecycle manager, health checks | Genesis |
| **Mnemosyne** | C++ | Metadata key-value store | Cassandra/IDF |
| **Hermes** | C++ | Block I/O service with replication | Stargate |
| **Themis** | C++ | Data balance scanner, rebalancer | Curator |
| **Oracle** | Python | I/O workload generator (master-side only) | UVM I/O |

Nodes auto-discover each other via UDP broadcast -- no static node list needed. Add a node by deploying Eidolon; remove one by shutting it down.

## Quick Start

### Prerequisites

- Ubuntu 22.04+ VMs on the same L2 subnet
- Two OS users per VM (master + candidate)
- SSH key-based auth between master users across nodes

### Bootstrap a Node

```bash
git clone <repo_url> ~/eidolon
cd ~/eidolon

# Optionally edit user names:
#   vi eidolon.conf

# Run the all-in-one bootstrap (requires sudo):
./tools/bootstrap.sh
```

This installs dependencies, builds C++ services, deploys to the candidate user, and starts the cluster. Repeat on each VM -- nodes discover each other automatically.

### Development Workflow

```bash
# After editing source code:
./tools/build.sh              # Build C++ + package Python
./tools/deploy.sh --local     # Deploy to local candidate user
./tools/deploy.sh             # Deploy to all cluster nodes
```

### Running Scenarios (Interviewer)

```bash
# List available scenarios:
./cli/eidolon-ctl scenarios list

# Apply a scenario (injects a fault):
./cli/eidolon-ctl scenarios apply 001_oom_mnemosyne

# Show progressive hints:
./cli/eidolon-ctl scenarios hint 001_oom_mnemosyne

# Check if the candidate fixed it:
./cli/eidolon-ctl scenarios verify 001_oom_mnemosyne

# Reset cluster to healthy state:
./cli/eidolon-ctl scenarios reset
```

### Candidate Experience

The candidate SSHs in as the candidate user and sees:

```
  ╔══════════════════════════════════════════════╗
  ║              Eidolon Cluster Node            ║
  ╠══════════════════════════════════════════════╣
  ║  Node IP  : 10.48.8.236
  ║  Version  : a1b2c3d
  ║  Hostname : borealusurper
  ╚══════════════════════════════════════════════╝

  Run 'cluster status' to see the cluster state.

ember@borealusurper:~$ cluster status

  Cluster Status (node: 10.48.8.236)
  ──────────────────────────────────────────────────

  Services (3):
  Service         Status   PID        Restarts
  ──────────────────────────────────────────────────
  hermes          UP       1234       0
  mnemosyne       UP       1235       0
  themis          UP       1236       0
```

## Configuration

`eidolon.conf` controls user names and network ports:

```ini
[users]
master = scorch
candidate = ember

[network]
discovery_port = 7100
styx_port = 7101
prometheus_port = 7102
mnemosyne_port = 7200
hermes_port = 7300
oracle_port = 7400

[build]
static_link = true
```

Service-specific settings are in `config/*.gflags` files using the `--flag=value` format.

## Adding a Scenario

1. Create a directory under `scenarios/` (e.g., `scenarios/006_my_scenario/`)
2. Write a `manifest.yaml` describing the fault, symptoms, hints, and solution
3. Define `actions` (what to break) and `verify` (how to check the fix)
4. Test with `eidolon-ctl scenarios apply/verify/reset`

See existing scenarios for examples.

## Project Structure

```
eidolon/
├── eidolon.conf           # User/network configuration
├── CMakeLists.txt         # C++ build system
├── requirements.txt       # Python dependencies
├── common/                # Shared C++ libraries (logging, networking, etc.)
├── services/
│   ├── hermes/            # C++ block I/O service
│   ├── mnemosyne/         # C++ metadata store
│   ├── themis/            # C++ data balancer
│   ├── prometheus/        # Python lifecycle manager
│   ├── styx/              # Python config coordinator
│   └── oracle/            # Python I/O orchestrator
├── cli/
│   ├── cluster            # Candidate-facing cluster CLI
│   └── eidolon-ctl        # Interviewer scenario management CLI
├── scenarios/             # Scenario definitions
├── config/                # Default gflag templates
├── packaging/             # Candidate bashrc, MOTD
└── tools/                 # Build, deploy, bootstrap scripts
```
