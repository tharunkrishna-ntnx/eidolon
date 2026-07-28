"""Simple in-memory key-value config store with persistence."""

import json
import logging
import os
import threading

logger = logging.getLogger("styx.config_store")


class ConfigStore:
    """Thread-safe key-value store backed by a JSON file."""

    def __init__(self, persist_path=None):
        self._data = {}
        self._lock = threading.Lock()
        self._persist_path = persist_path
        if persist_path and os.path.exists(persist_path):
            self._load()

    def get(self, key):
        with self._lock:
            return self._data.get(key)

    def put(self, key, value):
        with self._lock:
            self._data[key] = value
            self._persist()

    def delete(self, key):
        with self._lock:
            if key in self._data:
                del self._data[key]
                self._persist()
                return True
            return False

    def keys(self, prefix=""):
        with self._lock:
            if not prefix:
                return list(self._data.keys())
            return [k for k in self._data if k.startswith(prefix)]

    def snapshot(self):
        """Return a shallow copy of all data."""
        with self._lock:
            return dict(self._data)

    def load_from(self, data):
        """Replace local store contents with |data| dict."""
        with self._lock:
            self._data = dict(data)
            self._persist()

    def _persist(self):
        if not self._persist_path:
            return
        try:
            os.makedirs(os.path.dirname(self._persist_path), exist_ok=True)
            with open(self._persist_path, "w") as f:
                json.dump(self._data, f, indent=2)
        except OSError:
            logger.warning("Failed to persist config store", exc_info=True)

    def _load(self):
        try:
            with open(self._persist_path) as f:
                self._data = json.load(f)
            logger.info("Loaded %d keys from %s",
                        len(self._data), self._persist_path)
        except (OSError, json.JSONDecodeError):
            logger.warning("Failed to load config store from %s",
                           self._persist_path, exc_info=True)
