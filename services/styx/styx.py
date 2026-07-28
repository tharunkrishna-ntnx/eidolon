#!/usr/bin/env python3
"""Styx -- Config coordinator and service registry for Eidolon.

Provides:
  - UDP broadcast discovery for peer Styx nodes
  - Leader election (lowest IP wins)
  - Key-value config store over TCP
  - Service registry (services register endpoints for discovery)
"""

import argparse
import json
import logging
import os
import signal
import socket
import socketserver
import sys
import threading
import time

from config_store import ConfigStore
from discovery import DiscoveryBeacon, DiscoveryListener
from leader_election import LeaderElection

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    datefmt="%Y%m%d %H:%M:%S",
)
logger = logging.getLogger("styx")


class StyxState:
    """Shared mutable state for the Styx service."""

    def __init__(self, local_ip, config_dir, data_dir):
        self.local_ip = local_ip
        persist_path = os.path.join(data_dir, "styx_config.json") if data_dir else None
        self.config = ConfigStore(persist_path)
        self.election = LeaderElection(local_ip)

        # Service registry: service_name -> {ip:port, ...}
        self._registry = {}  # service -> list of "ip:port"
        self._reg_lock = threading.Lock()

    def register_service(self, service, ip, port):
        ep = f"{ip}:{port}"
        with self._reg_lock:
            if service not in self._registry:
                self._registry[service] = set()
            self._registry[service].add(ep)
        logger.info("Registered %s at %s", service, ep)

    def unregister_service(self, service, ip):
        with self._reg_lock:
            if service in self._registry:
                self._registry[service] = {
                    ep for ep in self._registry[service]
                    if not ep.startswith(ip + ":")
                }

    def lookup_service(self, service):
        with self._reg_lock:
            return list(self._registry.get(service, []))

    def all_services(self):
        with self._reg_lock:
            return {svc: list(eps) for svc, eps in self._registry.items()}


class StyxRequestHandler(socketserver.StreamRequestHandler):
    """Handle one TCP client connection with line-based commands."""

    def handle(self):
        state = self.server.styx_state
        peer = self.client_address[0]
        logger.debug("Client connected from %s", peer)

        try:
            while True:
                raw_line = self.rfile.readline()
                if not raw_line:
                    break
                line = raw_line.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                response = self._process(line, state)
                if response:
                    self.wfile.write(response.encode("utf-8"))
                    self.wfile.flush()
        except (ConnectionResetError, BrokenPipeError):
            pass

    def _process(self, line, state):
        parts = line.split(None, 3)
        cmd = parts[0].upper() if parts else ""

        if cmd == "REG" and len(parts) >= 4:
            service, ip, port = parts[1], parts[2], parts[3]
            state.register_service(service, ip, port)
            return "OK\n"

        elif cmd == "UNREG" and len(parts) >= 3:
            service, ip = parts[1], parts[2]
            state.unregister_service(service, ip)
            return "OK\n"

        elif cmd == "LOOKUP" and len(parts) >= 2:
            service = parts[1]
            endpoints = state.lookup_service(service)
            return f"ENDPOINTS {len(endpoints)}\n" + \
                   "".join(f"{ep}\n" for ep in endpoints)

        elif cmd == "MEMBERS":
            members = state.election.get_members()
            lines = [f"MEMBERS_LIST {len(members)}\n"]
            for ip, info in sorted(members.items()):
                age = int(time.time() - info["last_seen"])
                lines.append(f"{ip} UP (seen {age}s ago)\n")
            return "".join(lines)

        elif cmd == "KGET" and len(parts) >= 2:
            key = parts[1]
            val = state.config.get(key)
            if val is not None:
                sval = json.dumps(val) if not isinstance(val, str) else val
                return f"VALUE {len(sval)}\n{sval}\n"
            return "NOT_FOUND\n"

        elif cmd == "KPUT" and len(parts) >= 3:
            key = parts[1]
            # Remaining parts are the value.
            val = parts[2] if len(parts) == 3 else " ".join(parts[2:])
            state.config.put(key, val)
            return "OK\n"

        elif cmd == "LEADER":
            leader = state.election.leader_ip
            port = state.election.get_members().get(leader, {}).get(
                "styx_port", "7101")
            return f"LEADER_IS {leader}:{port}\n"

        elif cmd == "SERVICES":
            svcs = state.all_services()
            lines = [f"SERVICES {len(svcs)}\n"]
            for svc, eps in sorted(svcs.items()):
                lines.append(f"  {svc}: {', '.join(eps)}\n")
            return "".join(lines)

        return "ERROR Unknown command\n"


class StyxTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, handler, styx_state):
        self.styx_state = styx_state
        super().__init__(address, handler)


def get_local_ip():
    """Return the first non-loopback IPv4 address."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


def read_version(config_dir):
    """Read the Eidolon version from the deploy marker."""
    for path in [
        os.path.join(os.path.expanduser("~"), ".eidolon_version"),
        os.path.join(config_dir, "..", ".eidolon_version"),
    ]:
        if os.path.exists(path):
            return open(path).read().strip()
    return "dev"


def main():
    parser = argparse.ArgumentParser(description="Styx config coordinator")
    parser.add_argument("--port", type=int, default=7101)
    parser.add_argument("--discovery_port", type=int, default=7100)
    parser.add_argument("--beacon_interval_sec", type=int, default=5)
    parser.add_argument("--node_timeout_sec", type=int, default=30)
    parser.add_argument("--config_dir", default="")
    parser.add_argument("--log_dir", default="")
    parser.add_argument("--tmp_dir", default="")
    parser.add_argument("--data_dir", default="")
    args = parser.parse_args()

    if args.log_dir:
        os.makedirs(args.log_dir, exist_ok=True)
        fh = logging.FileHandler(os.path.join(args.log_dir, "styx.log"))
        fh.setFormatter(logging.Formatter(
            "%(asctime)s %(levelname)s [%(name)s] %(message)s"))
        logging.getLogger().addHandler(fh)

    local_ip = get_local_ip()
    version = read_version(args.config_dir)
    logger.info("Styx starting: ip=%s port=%d version=%s",
                local_ip, args.port, version)

    data_dir = args.data_dir or args.config_dir
    state = StyxState(local_ip, args.config_dir, data_dir)
    state.election.set_timeout(args.node_timeout_sec)
    state.election.node_seen(local_ip, args.port)

    # Write PID file.
    if args.tmp_dir:
        os.makedirs(args.tmp_dir, exist_ok=True)
        with open(os.path.join(args.tmp_dir, "styx.pid"), "w") as f:
            f.write(str(os.getpid()) + "\n")

    # Start discovery.
    beacon = DiscoveryBeacon(version, local_ip, args.port,
                             args.discovery_port, args.beacon_interval_sec)
    listener = DiscoveryListener(
        args.discovery_port, version,
        on_node_seen=state.election.node_seen,
        node_timeout=args.node_timeout_sec,
    )
    beacon.start()
    listener.start()

    # Timeout checker thread.
    def timeout_loop():
        while True:
            time.sleep(args.beacon_interval_sec)
            state.election.check_timeouts()

    threading.Thread(target=timeout_loop, daemon=True).start()

    # Start TCP server.
    server = StyxTCPServer(("0.0.0.0", args.port), StyxRequestHandler, state)

    def shutdown_handler(sig, frame):
        logger.info("Shutting down...")
        beacon.stop()
        listener.stop()
        server.shutdown()
        if args.tmp_dir:
            pid_path = os.path.join(args.tmp_dir, "styx.pid")
            if os.path.exists(pid_path):
                os.remove(pid_path)
        sys.exit(0)

    signal.signal(signal.SIGTERM, shutdown_handler)
    signal.signal(signal.SIGINT, shutdown_handler)

    logger.info("Styx TCP server listening on port %d", args.port)
    server.serve_forever()


if __name__ == "__main__":
    main()
