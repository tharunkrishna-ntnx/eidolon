"""UDP broadcast discovery for Styx nodes."""

import json
import logging
import socket
import struct
import threading
import time

logger = logging.getLogger("styx.discovery")

BEACON_MAGIC = b"EIDOLON\x01"


class DiscoveryBeacon:
    """Periodically broadcasts presence on the local subnet."""

    def __init__(self, version, node_ip, styx_port, discovery_port,
                 beacon_interval=5):
        self.version = version
        self.node_ip = node_ip
        self.styx_port = styx_port
        self.discovery_port = discovery_port
        self.beacon_interval = beacon_interval
        self._running = False
        self._thread = None
        self._sock = None

    def start(self):
        self._running = True
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._thread = threading.Thread(target=self._broadcast_loop,
                                        daemon=True)
        self._thread.start()
        logger.info("Discovery beacon started (port=%d, interval=%ds)",
                    self.discovery_port, self.beacon_interval)

    def stop(self):
        self._running = False
        if self._sock:
            self._sock.close()
        if self._thread:
            self._thread.join(timeout=5)

    def _build_payload(self):
        data = {
            "version": self.version,
            "node_ip": self.node_ip,
            "styx_port": self.styx_port,
            "epoch": int(time.time()),
        }
        body = json.dumps(data).encode("utf-8")
        return BEACON_MAGIC + body

    def _broadcast_loop(self):
        while self._running:
            try:
                payload = self._build_payload()
                self._sock.sendto(
                    payload,
                    ("<broadcast>", self.discovery_port)
                )
            except OSError:
                if self._running:
                    logger.warning("Beacon send failed", exc_info=True)
            time.sleep(self.beacon_interval)


class DiscoveryListener:
    """Listens for beacons from other Styx nodes."""

    def __init__(self, discovery_port, expected_version, on_node_seen,
                 node_timeout=30):
        self.discovery_port = discovery_port
        self.expected_version = expected_version
        self.on_node_seen = on_node_seen
        self.node_timeout = node_timeout
        self._running = False
        self._thread = None
        self._sock = None

    def start(self):
        self._running = True
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self._sock.bind(("", self.discovery_port))
        self._sock.settimeout(2.0)
        self._thread = threading.Thread(target=self._listen_loop,
                                        daemon=True)
        self._thread.start()
        logger.info("Discovery listener started on port %d",
                    self.discovery_port)

    def stop(self):
        self._running = False
        if self._sock:
            self._sock.close()
        if self._thread:
            self._thread.join(timeout=5)

    def _listen_loop(self):
        while self._running:
            try:
                data, addr = self._sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                if self._running:
                    logger.warning("Listen error", exc_info=True)
                continue

            if not data.startswith(BEACON_MAGIC):
                continue

            try:
                payload = json.loads(data[len(BEACON_MAGIC):])
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue

            version = payload.get("version", "")
            if version != self.expected_version:
                logger.debug("Ignoring beacon from %s (version %s != %s)",
                             addr[0], version, self.expected_version)
                continue

            node_ip = payload.get("node_ip", addr[0])
            styx_port = payload.get("styx_port", 7101)
            self.on_node_seen(node_ip, styx_port)
