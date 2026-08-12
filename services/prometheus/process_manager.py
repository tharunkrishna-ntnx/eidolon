"""Process manager for Eidolon services."""

import logging
import os
import signal
import subprocess
import threading
import time

logger = logging.getLogger("prometheus.procmgr")


class ManagedProcess:
    """Tracks a single managed service process."""

    def __init__(self, name, command, log_dir, tmp_dir,
                 max_restarts=3):
        self.name = name
        self.command = command
        self.log_dir = log_dir
        self.tmp_dir = tmp_dir
        self.max_restarts = max_restarts
        self.proc = None
        self.restart_count = 0
        self.started_at = None
        self.stopped = False

    @property
    def pid(self):
        return self.proc.pid if self.proc and self.proc.poll() is None else None

    @property
    def is_running(self):
        return self.proc is not None and self.proc.poll() is None

    @property
    def status(self):
        if self.stopped:
            return "STOPPED"
        if self.is_running:
            return "UP"
        return "DOWN"

    def start(self):
        if self.is_running:
            return
        self.stopped = False
        stdout_path = os.path.join(self.log_dir, f"{self.name}_stdout.log")
        os.makedirs(self.log_dir, exist_ok=True)
        os.makedirs(self.tmp_dir, exist_ok=True)

        with open(stdout_path, "a") as stdout_f:
            self.proc = subprocess.Popen(
                self.command,
                stdout=stdout_f,
                stderr=subprocess.STDOUT,
                preexec_fn=os.setsid,
            )
        self.started_at = time.time()
        logger.info("Started %s (pid=%d): %s",
                    self.name, self.proc.pid, " ".join(self.command))

    def stop(self):
        self.stopped = True
        if not self.is_running:
            return
        try:
            os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
            self.proc.wait(timeout=10)
        except (ProcessLookupError, subprocess.TimeoutExpired):
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
                self.proc.wait(timeout=5)
            except Exception:
                pass
        logger.info("Stopped %s", self.name)

    def restart(self):
        self.stop()
        time.sleep(1)
        self.restart_count += 1
        self.start()

    def check_health(self):
        """Returns True if healthy, False if crashed and needs attention."""
        if self.stopped:
            return True
        if self.is_running:
            return True
        # Process died unexpectedly.
        exit_code = self.proc.returncode if self.proc else None
        logger.warning("%s exited unexpectedly (code=%s, restarts=%d/%d)",
                       self.name, exit_code, self.restart_count,
                       self.max_restarts)
        return False


class ProcessManager:
    """Manages multiple service processes with health checking."""

    def __init__(self, candidate_home, config_dir, log_dir, tmp_dir,
                 max_restarts=3):
        self.candidate_home = candidate_home
        self.config_dir = config_dir
        self.log_dir = log_dir
        self.tmp_dir = tmp_dir
        self.max_restarts = max_restarts
        self._services = {}  # name -> ManagedProcess
        self._lock = threading.Lock()

    def register(self, name, command):
        """Register a service to be managed."""
        with self._lock:
            self._services[name] = ManagedProcess(
                name, command, self.log_dir, self.tmp_dir,
                self.max_restarts,
            )

    def start(self, name=None):
        """Start a specific service or all services."""
        with self._lock:
            targets = [name] if name else list(self._services.keys())
        for svc_name in targets:
            svc = self._services.get(svc_name)
            if svc:
                svc.start()

    def stop(self, name=None):
        """Stop a specific service or all services."""
        with self._lock:
            targets = [name] if name else list(self._services.keys())
        for svc_name in targets:
            svc = self._services.get(svc_name)
            if svc:
                svc.stop()

    def restart(self, name=None):
        """Restart a specific service or all services."""
        with self._lock:
            targets = [name] if name else list(self._services.keys())
        for svc_name in targets:
            svc = self._services.get(svc_name)
            if svc:
                svc.restart()

    def status(self):
        """Return status dict: {name: {status, pid, restarts}}."""
        result = {}
        with self._lock:
            for name, svc in self._services.items():
                result[name] = {
                    "status": svc.status,
                    "pid": svc.pid,
                    "restarts": svc.restart_count,
                }
        return result

    def health_check_all(self):
        """Check all services, auto-restart crashed ones."""
        with self._lock:
            services = list(self._services.values())
        for svc in services:
            if not svc.check_health():
                if svc.restart_count < svc.max_restarts:
                    logger.info("Auto-restarting %s", svc.name)
                    svc.restart()
                else:
                    logger.error(
                        "%s exceeded max restarts (%d), not restarting",
                        svc.name, svc.max_restarts)

    def stop_all(self):
        """Stop all managed services."""
        self.stop()
