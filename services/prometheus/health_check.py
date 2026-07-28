"""Health check utilities for Prometheus."""

import logging
import socket

logger = logging.getLogger("prometheus.health")


def check_tcp_port(host, port, timeout=3):
    """Return True if a TCP connection can be established."""
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except (OSError, socket.timeout):
        return False


def check_service_stats(host, port, timeout=3):
    """Send a STATS command to a service and return the response."""
    try:
        with socket.create_connection((host, port), timeout=timeout) as s:
            s.sendall(b"STATS\n")
            data = s.recv(4096)
            return data.decode("utf-8", errors="replace").strip()
    except (OSError, socket.timeout):
        return None
