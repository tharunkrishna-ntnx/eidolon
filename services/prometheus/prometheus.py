#!/usr/bin/env python3
"""Prometheus -- Lifecycle manager for Eidolon cluster services.

Manages local service processes, performs health checks, and coordinates
with peer Prometheus instances for cluster-wide operations.
"""

import argparse
import json
import logging
import os
import signal
import socket
import struct
import sys
import threading
import time

from process_manager import ProcessManager
from health_check import check_tcp_port

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s [%(name)s] %(message)s",
    datefmt="%Y%m%d %H:%M:%S",
)
logger = logging.getLogger("prometheus")


def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except OSError:
        return "127.0.0.1"


def build_service_command(candidate_home, service_name, config_dir,
                          log_dir, tmp_dir, data_dir, extra_args=None):
    """Build the command list to launch a service binary."""
    bin_path = os.path.join(candidate_home, "bin", service_name)
    cmd = [bin_path,
           f"--config_dir={config_dir}",
           f"--eidolon_log_dir={log_dir}",
           f"--tmp_dir={tmp_dir}"]

    if service_name == "mnemosyne":
        cmd.append(f"--data_dir={os.path.join(data_dir, 'mnemosyne')}")
    elif service_name == "hermes":
        cmd.append(f"--blocks_dir={os.path.join(data_dir, 'hermes', 'blocks')}")
    elif service_name == "styx":
        cmd.append(f"--data_dir={os.path.join(data_dir, 'styx')}")

    if extra_args:
        cmd.extend(extra_args)
    return cmd


class PrometheusServer:
    """Unix-socket server for the cluster CLI to communicate with."""

    def __init__(self, socket_path, process_mgr, local_ip, styx_host,
                 styx_port):
        self.socket_path = socket_path
        self.process_mgr = process_mgr
        self.local_ip = local_ip
        self.styx_host = styx_host
        self.styx_port = styx_port
        self._running = False
        self._thread = None
        self._sock = None

    def start(self):
        self._running = True
        if os.path.exists(self.socket_path):
            os.remove(self.socket_path)
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._sock.bind(self.socket_path)
        os.chmod(self.socket_path, 0o770)
        self._sock.listen(5)
        self._sock.settimeout(2.0)
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()
        logger.info("Prometheus control socket: %s", self.socket_path)

    def stop(self):
        self._running = False
        if self._sock:
            self._sock.close()
        if os.path.exists(self.socket_path):
            os.remove(self.socket_path)

    def _accept_loop(self):
        while self._running:
            try:
                conn, _ = self._sock.accept()
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
            response = self._process_command(data)
            conn.sendall(response.encode("utf-8"))
        except Exception:
            logger.warning("Error handling CLI command", exc_info=True)
        finally:
            conn.close()

    def _process_command(self, line):
        parts = line.split()
        cmd = parts[0].lower() if parts else ""

        if cmd == "status":
            return self._format_status()
        elif cmd == "start":
            name = parts[1] if len(parts) > 1 else None
            self.process_mgr.start(name)
            return "OK\n"
        elif cmd == "stop":
            name = parts[1] if len(parts) > 1 else None
            self.process_mgr.stop(name)
            return "OK\n"
        elif cmd == "restart":
            name = parts[1] if len(parts) > 1 else None
            self.process_mgr.restart(name)
            return "OK\n"
        elif cmd == "members":
            return self._query_styx("MEMBERS\n")
        elif cmd == "services":
            return self._query_styx("SERVICES\n")
        return "ERROR Unknown command\n"

    def _format_status(self):
        status = self.process_mgr.status()
        members = self._get_members()

        lines = []
        lines.append(f"CLUSTER_STATUS {self.local_ip}\n")
        lines.append(f"MEMBERS {len(members)}\n")
        for ip in sorted(members):
            lines.append(f"  {ip}\n")
        lines.append(f"LOCAL_SERVICES {len(status)}\n")
        for name, info in sorted(status.items()):
            pid_str = str(info["pid"]) if info["pid"] else "-"
            lines.append(
                f"  {name:<15s} {info['status']:<8s} pid={pid_str} "
                f"restarts={info['restarts']}\n"
            )
        return "".join(lines)

    def _get_members(self):
        try:
            resp = self._query_styx("MEMBERS\n")
            members = []
            for line in resp.strip().split("\n"):
                parts = line.split()
                if parts and parts[0][0].isdigit():
                    members.append(parts[0])
            return members
        except Exception:
            return [self.local_ip]

    def _query_styx(self, command):
        try:
            with socket.create_connection(
                    (self.styx_host, self.styx_port), timeout=5) as s:
                s.sendall(command.encode("utf-8"))
                s.shutdown(socket.SHUT_WR)
                chunks = []
                while True:
                    chunk = s.recv(4096)
                    if not chunk:
                        break
                    chunks.append(chunk.decode("utf-8"))
                return "".join(chunks)
        except (OSError, socket.timeout):
            return "ERROR Cannot reach Styx\n"


def main():
    parser = argparse.ArgumentParser(description="Prometheus lifecycle manager")
    parser.add_argument("--port", type=int, default=7102)
    parser.add_argument("--health_check_interval_sec", type=int, default=10)
    parser.add_argument("--max_restart_attempts", type=int, default=3)
    parser.add_argument("--styx_host", default="127.0.0.1")
    parser.add_argument("--styx_port", type=int, default=7101)
    parser.add_argument("--config_dir", default="")
    parser.add_argument("--log_dir", default="")
    parser.add_argument("--tmp_dir", default="")
    parser.add_argument("--data_dir", default="")
    parser.add_argument("--candidate_home", default="")

    # Accept and ignore unknown flags from gflags-style configs.
    args, _ = parser.parse_known_args()

    if args.log_dir:
        os.makedirs(args.log_dir, exist_ok=True)
        fh = logging.FileHandler(os.path.join(args.log_dir, "prometheus.log"))
        fh.setFormatter(logging.Formatter(
            "%(asctime)s %(levelname)s [%(name)s] %(message)s"))
        logging.getLogger().addHandler(fh)

    local_ip = get_local_ip()
    candidate_home = args.candidate_home or os.path.expanduser("~")
    config_dir = args.config_dir or os.path.join(candidate_home, "config")
    log_dir = args.log_dir or os.path.join(candidate_home, "data", "logs")
    tmp_dir = args.tmp_dir or os.path.join(candidate_home, "tmp")
    data_dir = args.data_dir or os.path.join(candidate_home, "data")

    logger.info("Prometheus starting: ip=%s candidate_home=%s",
                local_ip, candidate_home)

    # Write PID file.
    os.makedirs(tmp_dir, exist_ok=True)
    with open(os.path.join(tmp_dir, "prometheus.pid"), "w") as f:
        f.write(str(os.getpid()) + "\n")

    # Set up process manager.
    pm = ProcessManager(candidate_home, config_dir, log_dir, tmp_dir,
                        args.max_restart_attempts)

    # Register managed services (Styx is expected to already be running).
    managed_services = ["mnemosyne", "hermes", "themis", "atlas"]
    for svc in managed_services:
        bin_path = os.path.join(candidate_home, "bin", svc)
        if os.path.exists(bin_path):
            cmd = build_service_command(
                candidate_home, svc, config_dir, log_dir, tmp_dir, data_dir)
            pm.register(svc, cmd)
        else:
            logger.warning("Binary not found for %s, skipping", svc)

    # Start the Prometheus control server (Unix socket).
    socket_path = os.path.join(tmp_dir, "prometheus.sock")
    server = PrometheusServer(socket_path, pm, local_ip,
                              args.styx_host, args.styx_port)
    server.start()

    # Register with Styx.
    try:
        with socket.create_connection(
                (args.styx_host, args.styx_port), timeout=5) as s:
            s.sendall(f"REG prometheus {local_ip} {args.port}\n".encode())
            s.shutdown(socket.SHUT_WR)
            s.recv(256)
            logger.info("Registered with Styx")
    except (OSError, socket.timeout):
        logger.warning("Could not register with Styx")

    # Start all managed services.
    pm.start()

    # Health check loop.
    running = True

    def shutdown_handler(sig, frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, shutdown_handler)
    signal.signal(signal.SIGINT, shutdown_handler)

    logger.info("Prometheus running, health check interval=%ds",
                args.health_check_interval_sec)

    while running:
        time.sleep(args.health_check_interval_sec)
        pm.health_check_all()

    # Shutdown.
    logger.info("Prometheus shutting down...")
    pm.stop_all()
    server.stop()
    pid_path = os.path.join(tmp_dir, "prometheus.pid")
    if os.path.exists(pid_path):
        os.remove(pid_path)
    logger.info("Prometheus stopped")


if __name__ == "__main__":
    main()
