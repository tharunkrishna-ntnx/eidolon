#!/usr/bin/env python3
"""Oracle -- I/O orchestrator for Eidolon.

Runs on the master user side and generates mock UVM I/O workload against
Hermes instances in the cluster.  Controlled via a TCP command port and
workload profiles.
"""

import argparse
import json
import logging
import os
import random
import signal
import socket
import string
import struct
import sys
import threading
import time
import uuid

try:
    import yaml
    HAS_YAML = True
except ImportError:
    HAS_YAML = False

from workload_profiles import ProfileManager, DEFAULT_PROFILES

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    datefmt="%Y%m%d %H:%M:%S",
)
logger = logging.getLogger("oracle")


def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


class HermesClient:
    """Simple client for sending I/O to a Hermes instance."""

    def __init__(self, ip, port, timeout=5):
        self.ip = ip
        self.port = port
        self.timeout = timeout

    def write_block(self, block_id, data):
        try:
            with socket.create_connection(
                    (self.ip, self.port), timeout=self.timeout) as s:
                header = f"WRITE {block_id} {len(data)}\n".encode()
                s.sendall(header + data + b"\n")
                resp = s.recv(256).decode()
                return "OK" in resp
        except (OSError, socket.timeout):
            return False

    def read_block(self, block_id):
        try:
            with socket.create_connection(
                    (self.ip, self.port), timeout=self.timeout) as s:
                s.sendall(f"READ {block_id}\n".encode())
                resp = s.recv(256)
                return b"DATA" in resp
        except (OSError, socket.timeout):
            return False

    def delete_block(self, block_id):
        try:
            with socket.create_connection(
                    (self.ip, self.port), timeout=self.timeout) as s:
                s.sendall(f"DELETE {block_id}\n".encode())
                resp = s.recv(256).decode()
                return "OK" in resp
        except (OSError, socket.timeout):
            return False


class OracleEngine:
    """Generates I/O workload based on the active profile."""

    def __init__(self, profile_mgr, styx_host, styx_port, hermes_port,
                 hermes_limit_gb=40, mnemosyne_limit_gb=20):
        self.profile_mgr = profile_mgr
        self.styx_host = styx_host
        self.styx_port = styx_port
        self.hermes_port = hermes_port
        self.hermes_limit_bytes = hermes_limit_gb * 1024 ** 3
        self.mnemosyne_limit_bytes = mnemosyne_limit_gb * 1024 ** 3
        self._running = False
        self._thread = None
        self._written_blocks = []
        self._lock = threading.Lock()
        self._cached_nodes = []
        self._last_discovery = 0
        self._writes_paused = False
        self._last_usage_check = 0.0
        self._usage_check_interval = 15  # seconds

        self.stats = {
            "total_creates": 0,
            "total_reads": 0,
            "total_updates": 0,
            "total_deletes": 0,
            "failed_creates": 0,
            "failed_reads": 0,
            "failed_updates": 0,
            "failed_deletes": 0,
        }

    def start(self):
        self._running = True
        self._thread = threading.Thread(target=self._io_loop, daemon=True)
        self._thread.start()
        logger.info("Oracle I/O engine started")

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=10)
        logger.info("Oracle I/O engine stopped")

    def _discover_hermes_nodes(self):
        """Query Styx for Hermes endpoints (cached for 10s)."""
        now = time.time()
        if self._cached_nodes and now - self._last_discovery < 10:
            return self._cached_nodes

        try:
            with socket.create_connection(
                    (self.styx_host, self.styx_port), timeout=3) as s:
                s.sendall(b"LOOKUP hermes\n")
                s.shutdown(socket.SHUT_WR)
                chunks = []
                while True:
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    chunks.append(chunk)
                resp = b"".join(chunks).decode()
        except (OSError, socket.timeout):
            logger.warning("Cannot discover Hermes nodes via Styx")
            return self._cached_nodes

        nodes = []
        for line in resp.strip().split("\n"):
            line = line.strip()
            if ":" in line and line[0].isdigit():
                ip, port = line.rsplit(":", 1)
                try:
                    nodes.append((ip, int(port)))
                except ValueError:
                    pass

        if nodes:
            self._cached_nodes = nodes
            self._last_discovery = now
        return nodes

    def _query_service_stats(self, ip, port):
        """Query a service's STATS endpoint, return parsed JSON dict or None."""
        try:
            with socket.create_connection((ip, port), timeout=3) as s:
                s.sendall(b"STATS\n")
                s.shutdown(socket.SHUT_WR)
                chunks = []
                while True:
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    chunks.append(chunk)
                resp = b"".join(chunks).decode()
                # Response format: "STATS_REPLY {...json...}\n"
                if resp.startswith("STATS_REPLY "):
                    return json.loads(resp[len("STATS_REPLY "):].strip())
        except (OSError, socket.timeout, json.JSONDecodeError, ValueError):
            pass
        return None

    def _check_storage_usage(self, hermes_nodes):
        """Check total storage across Hermes and Mnemosyne nodes.

        Returns True if writes should be paused.
        """
        now = time.time()
        if now - self._last_usage_check < self._usage_check_interval:
            return self._writes_paused
        self._last_usage_check = now

        # Sum Hermes total_bytes across all nodes.
        hermes_total = 0
        for ip, port in hermes_nodes:
            stats = self._query_service_stats(ip, port)
            if stats:
                hermes_total += stats.get("total_bytes", 0)

        # Sum Mnemosyne memory_used_bytes across all nodes.
        mnemosyne_total = 0
        mnemosyne_nodes = self._discover_mnemosyne_nodes()
        for ip, port in mnemosyne_nodes:
            stats = self._query_service_stats(ip, port)
            if stats:
                mnemosyne_total += stats.get("memory_used_bytes", 0)

        hermes_over = hermes_total >= self.hermes_limit_bytes
        mnemosyne_over = mnemosyne_total >= self.mnemosyne_limit_bytes

        was_paused = self._writes_paused
        self._writes_paused = hermes_over or mnemosyne_over

        if self._writes_paused and not was_paused:
            reasons = []
            if hermes_over:
                reasons.append(
                    f"Hermes {hermes_total / (1024**3):.1f}GB "
                    f">= {self.hermes_limit_bytes / (1024**3):.0f}GB limit")
            if mnemosyne_over:
                reasons.append(
                    f"Mnemosyne {mnemosyne_total / (1024**3):.1f}GB "
                    f">= {self.mnemosyne_limit_bytes / (1024**3):.0f}GB limit")
            logger.warning("Writes PAUSED: %s", "; ".join(reasons))
        elif was_paused and not self._writes_paused:
            logger.info("Writes RESUMED: storage below limits")

        return self._writes_paused

    def _discover_mnemosyne_nodes(self):
        """Query Styx for Mnemosyne endpoints."""
        try:
            with socket.create_connection(
                    (self.styx_host, self.styx_port), timeout=3) as s:
                s.sendall(b"LOOKUP mnemosyne\n")
                s.shutdown(socket.SHUT_WR)
                chunks = []
                while True:
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    chunks.append(chunk)
                resp = b"".join(chunks).decode()
        except (OSError, socket.timeout):
            return []

        nodes = []
        for line in resp.strip().split("\n"):
            line = line.strip()
            if ":" in line and line[0].isdigit():
                ip, port = line.rsplit(":", 1)
                try:
                    nodes.append((ip, int(port)))
                except ValueError:
                    pass
        return nodes

    def _pick_op(self, profile, writes_paused):
        """Return one of 'create', 'read', 'update', 'delete' using weighted
        random selection based on profile IOPS.  Mutating ops (create/update)
        are suppressed when writes_paused is True."""
        create = profile.get("create_iops", profile.get("write_iops", 0))
        read   = profile.get("read_iops", 0)
        update = profile.get("update_iops", 0)
        delete = profile.get("delete_iops", 0)

        if writes_paused:
            create = 0
            update = 0

        weights = [create, read, update, delete]
        total = sum(weights)
        if total <= 0:
            return None
        r = random.random() * total
        cumulative = 0
        for op, w in zip(("create", "read", "update", "delete"), weights):
            cumulative += w
            if r < cumulative:
                return op
        return "read"

    def _total_iops(self, profile):
        """Sum of all operation IOPS from a profile."""
        return (profile.get("create_iops", profile.get("write_iops", 0))
                + profile.get("read_iops", 0)
                + profile.get("update_iops", 0)
                + profile.get("delete_iops", 0))

    def _effective_iops(self, profile, writes_paused):
        """IOPS remaining when mutating ops are optionally suppressed."""
        create = profile.get("create_iops", profile.get("write_iops", 0))
        read   = profile.get("read_iops", 0)
        update = profile.get("update_iops", 0)
        delete = profile.get("delete_iops", 0)
        if writes_paused:
            create = 0
            update = 0
        return create + read + update + delete

    def _io_loop(self):
        while self._running:
            profile = self.profile_mgr.active
            block_size_kb = profile.get("block_size_kb", 64)
            total_iops = self._total_iops(profile)

            if total_iops <= 0:
                time.sleep(1)
                continue

            nodes = self._discover_hermes_nodes()
            if not nodes:
                logger.warning("No Hermes nodes available, waiting...")
                time.sleep(5)
                continue

            writes_paused = self._check_storage_usage(nodes)

            # Determine target nodes.
            target = profile.get("target_nodes", "all")
            if target != "all" and isinstance(target, list):
                filtered = []
                for idx in target:
                    if isinstance(idx, int) and 0 <= idx - 1 < len(nodes):
                        filtered.append(nodes[idx - 1])
                if filtered:
                    nodes = filtered

            effective_total = self._effective_iops(profile, writes_paused)
            if effective_total <= 0:
                time.sleep(1)
                continue
            effective_interval = 1.0 / effective_total

            ops_this_second = 0
            second_start = time.time()

            while self._running and ops_this_second < effective_total:
                node = random.choice(nodes)
                client = HermesClient(node[0], node[1])
                op = self._pick_op(profile, writes_paused)

                if op == "create":
                    block_id = f"blk_{uuid.uuid4().hex[:12]}"
                    data = os.urandom(block_size_kb * 1024)
                    ok = client.write_block(block_id, data)
                    self.stats["total_creates"] += 1
                    if ok:
                        with self._lock:
                            self._written_blocks.append(block_id)
                            if len(self._written_blocks) > 10000:
                                self._written_blocks = self._written_blocks[-5000:]
                    else:
                        self.stats["failed_creates"] += 1

                elif op == "read":
                    with self._lock:
                        if self._written_blocks:
                            block_id = random.choice(self._written_blocks)
                        else:
                            block_id = "nonexistent"
                    ok = client.read_block(block_id)
                    self.stats["total_reads"] += 1
                    if not ok:
                        self.stats["failed_reads"] += 1

                elif op == "update":
                    with self._lock:
                        if self._written_blocks:
                            block_id = random.choice(self._written_blocks)
                        else:
                            block_id = None
                    if block_id:
                        data = os.urandom(block_size_kb * 1024)
                        ok = client.write_block(block_id, data)
                        self.stats["total_updates"] += 1
                        if not ok:
                            self.stats["failed_updates"] += 1
                    else:
                        # Nothing to update yet; fall through as a no-op.
                        pass

                elif op == "delete":
                    with self._lock:
                        if self._written_blocks:
                            idx = random.randrange(len(self._written_blocks))
                            block_id = self._written_blocks.pop(idx)
                        else:
                            block_id = None
                    if block_id:
                        ok = client.delete_block(block_id)
                        self.stats["total_deletes"] += 1
                        if not ok:
                            self.stats["failed_deletes"] += 1

                ops_this_second += 1

                elapsed = time.time() - second_start
                expected = ops_this_second * effective_interval
                if expected > elapsed:
                    time.sleep(expected - elapsed)

            elapsed = time.time() - second_start
            if elapsed < 1.0:
                time.sleep(1.0 - elapsed)


class OracleControlServer:
    """TCP server for controlling Oracle at runtime."""

    def __init__(self, port, profile_mgr, engine):
        self.port = port
        self.profile_mgr = profile_mgr
        self.engine = engine
        self._running = False
        self._thread = None
        self._sock = None

    def start(self):
        self._running = True
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind(("0.0.0.0", self.port))
        self._sock.listen(5)
        self._sock.settimeout(2.0)
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()
        logger.info("Oracle control server on port %d", self.port)

    def stop(self):
        self._running = False
        if self._sock:
            self._sock.close()

    def _accept_loop(self):
        while self._running:
            try:
                conn, addr = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=self._handle, args=(conn,),
                             daemon=True).start()

    def _handle(self, conn):
        try:
            data = conn.recv(4096).decode("utf-8").strip()
            if not data:
                return
            response = self._process(data)
            conn.sendall(response.encode("utf-8"))
        except Exception:
            logger.warning("Error handling control command", exc_info=True)
        finally:
            conn.close()

    def _process(self, line):
        parts = line.split(None, 1)
        cmd = parts[0].upper() if parts else ""
        arg = parts[1] if len(parts) > 1 else ""

        if cmd == "STATUS":
            profile = self.profile_mgr.active
            stats = self.engine.stats
            result = {
                "active_profile": self.profile_mgr.active_name,
                "profile": profile,
                "stats": stats,
                "writes_paused": self.engine._writes_paused,
                "limits": {
                    "hermes_gb": self.engine.hermes_limit_bytes / (1024 ** 3),
                    "mnemosyne_gb": self.engine.mnemosyne_limit_bytes / (1024 ** 3),
                },
            }
            return json.dumps(result, indent=2) + "\n"

        elif cmd == "PROFILE":
            if not arg:
                return f"ACTIVE {self.profile_mgr.active_name}\n"
            if self.profile_mgr.switch(arg):
                return "OK\n"
            return f"ERROR Unknown profile '{arg}'\n"

        elif cmd == "OVERRIDE":
            try:
                overrides = json.loads(arg)
                self.profile_mgr.apply_overrides(overrides)
                return "OK\n"
            except json.JSONDecodeError:
                return "ERROR Invalid JSON\n"

        elif cmd == "RESET":
            self.profile_mgr.reset()
            return "OK\n"

        elif cmd == "PROFILES":
            profiles = self.profile_mgr.list_profiles()
            return json.dumps(profiles, indent=2) + "\n"

        return "ERROR Unknown command\n"


def load_config(path):
    """Load Oracle config from YAML file."""
    if not path or not os.path.exists(path):
        return {"default_profile": "steady", "profiles": DEFAULT_PROFILES}

    if HAS_YAML:
        with open(path) as f:
            return yaml.safe_load(f)
    else:
        # Fallback: return defaults.
        logger.warning("PyYAML not available, using default profiles")
        return {"default_profile": "steady", "profiles": DEFAULT_PROFILES}


def main():
    parser = argparse.ArgumentParser(description="Oracle I/O orchestrator")
    parser.add_argument("--port", type=int, default=7400,
                        help="TCP control port")
    parser.add_argument("--config", default="",
                        help="Path to oracle.yaml")
    parser.add_argument("--styx_host", default="127.0.0.1")
    parser.add_argument("--styx_port", type=int, default=7101)
    parser.add_argument("--hermes_port", type=int, default=7300)
    parser.add_argument("--hermes_limit_gb", type=int, default=40,
                        help="Pause writes when total Hermes storage exceeds this (GB)")
    parser.add_argument("--mnemosyne_limit_gb", type=int, default=20,
                        help="Pause writes when total Mnemosyne usage exceeds this (GB)")
    parser.add_argument("--log_dir", default="")
    args = parser.parse_args()

    if args.log_dir:
        os.makedirs(args.log_dir, exist_ok=True)
        fh = logging.FileHandler(os.path.join(args.log_dir, "oracle.log"))
        fh.setFormatter(logging.Formatter(
            "%(asctime)s %(levelname)s [%(name)s] %(message)s"))
        logging.getLogger().addHandler(fh)

    # Load configuration.
    config_path = args.config
    if not config_path:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        config_path = os.path.join(script_dir, "oracle.yaml")
    config = load_config(config_path)

    profiles = config.get("profiles", DEFAULT_PROFILES)
    default_profile = config.get("default_profile", "steady")
    profile_mgr = ProfileManager(profiles, default_profile)

    logger.info("Oracle starting (control_port=%d, default_profile=%s, "
                "hermes_limit=%dGB, mnemosyne_limit=%dGB)",
                args.port, default_profile,
                args.hermes_limit_gb, args.mnemosyne_limit_gb)

    engine = OracleEngine(profile_mgr, args.styx_host, args.styx_port,
                          args.hermes_port,
                          hermes_limit_gb=args.hermes_limit_gb,
                          mnemosyne_limit_gb=args.mnemosyne_limit_gb)
    control = OracleControlServer(args.port, profile_mgr, engine)

    engine.start()
    control.start()

    running = True

    def shutdown_handler(sig, frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, shutdown_handler)
    signal.signal(signal.SIGINT, shutdown_handler)

    logger.info("Oracle running")
    while running:
        time.sleep(1)

    logger.info("Oracle shutting down...")
    engine.stop()
    control.stop()
    logger.info("Oracle stopped")


if __name__ == "__main__":
    main()
