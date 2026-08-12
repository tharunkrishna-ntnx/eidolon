"""Simple leader election: lowest IP among live members wins."""

import logging
import threading
import time

logger = logging.getLogger("styx.leader")


class LeaderElection:
    def __init__(self, local_ip, on_leader_change=None):
        self.local_ip = local_ip
        self.on_leader_change = on_leader_change
        self._members = {}  # ip -> last_seen timestamp
        self._lock = threading.Lock()
        self._leader_ip = None
        self._timeout = 30  # seconds

    @property
    def leader_ip(self):
        with self._lock:
            return self._leader_ip

    @property
    def is_leader(self):
        with self._lock:
            return self._leader_ip == self.local_ip

    def node_seen(self, ip, styx_port):
        """Called when a beacon is received from a node."""
        with self._lock:
            is_new = ip not in self._members
            self._members[ip] = {
                "last_seen": time.time(),
                "styx_port": styx_port,
            }
        if is_new:
            logger.info("New node discovered: %s", ip)
        self._recompute_leader()

    def set_timeout(self, seconds):
        self._timeout = seconds

    def check_timeouts(self):
        """Remove nodes that haven't sent beacons recently."""
        now = time.time()
        removed = []
        with self._lock:
            for ip in list(self._members):
                if now - self._members[ip]["last_seen"] > self._timeout:
                    del self._members[ip]
                    removed.append(ip)
        for ip in removed:
            logger.warning("Node %s timed out, removed from membership", ip)
        if removed:
            self._recompute_leader()

    def get_members(self):
        """Return dict of {ip: info} for all live members."""
        with self._lock:
            return dict(self._members)

    def live_ips(self):
        with self._lock:
            return sorted(self._members.keys())

    def _recompute_leader(self):
        with self._lock:
            ips = sorted(self._members.keys())
            new_leader = ips[0] if ips else None
            if new_leader != self._leader_ip:
                old = self._leader_ip
                self._leader_ip = new_leader
                logger.info("Leader changed: %s -> %s", old, new_leader)
                if self.on_leader_change:
                    threading.Thread(
                        target=self.on_leader_change,
                        args=(new_leader,),
                        daemon=True
                    ).start()
