"""Workload profile definitions and management for Oracle."""

import copy
import logging

logger = logging.getLogger("oracle.profiles")

DEFAULT_PROFILES = {
    "steady": {
        "create_iops": 30,
        "read_iops": 80,
        "update_iops": 15,
        "delete_iops": 5,
        "block_size_kb": 64,
        "pattern": "random",
        "target_nodes": "all",
    },
    "burst": {
        "create_iops": 800,
        "read_iops": 1500,
        "update_iops": 400,
        "delete_iops": 100,
        "block_size_kb": 4,
        "pattern": "random",
        "duration_sec": 60,
        "target_nodes": "all",
    },
    "sequential_heavy": {
        "create_iops": 400,
        "read_iops": 0,
        "update_iops": 80,
        "delete_iops": 20,
        "block_size_kb": 1024,
        "pattern": "sequential",
        "target_nodes": "all",
    },
    "churn": {
        "create_iops": 50,
        "read_iops": 50,
        "update_iops": 50,
        "delete_iops": 50,
        "block_size_kb": 64,
        "pattern": "random",
        "target_nodes": "all",
    },
    "idle": {
        "create_iops": 0,
        "read_iops": 0,
        "update_iops": 0,
        "delete_iops": 0,
        "block_size_kb": 64,
        "pattern": "random",
        "target_nodes": "all",
    },
}


class ProfileManager:
    """Manages workload profiles and the currently active profile."""

    def __init__(self, profiles=None, default_profile="steady"):
        self.profiles = copy.deepcopy(profiles or DEFAULT_PROFILES)
        self.default_profile_name = default_profile
        self._active_name = default_profile
        self._active = copy.deepcopy(
            self.profiles.get(default_profile, DEFAULT_PROFILES["steady"])
        )
        self._overrides = {}

    @property
    def active_name(self):
        return self._active_name

    @property
    def active(self):
        """Return the currently active profile with any overrides applied."""
        result = copy.deepcopy(self._active)
        result.update(self._overrides)
        return result

    def switch(self, profile_name):
        """Switch to a named profile."""
        if profile_name not in self.profiles:
            logger.warning("Unknown profile '%s', staying on '%s'",
                           profile_name, self._active_name)
            return False
        self._active_name = profile_name
        self._active = copy.deepcopy(self.profiles[profile_name])
        self._overrides = {}
        logger.info("Switched to profile '%s'", profile_name)
        return True

    def apply_overrides(self, overrides):
        """Apply temporary overrides to the active profile."""
        self._overrides.update(overrides)
        logger.info("Applied overrides: %s", overrides)

    def clear_overrides(self):
        self._overrides = {}

    def reset(self):
        """Reset to the default profile with no overrides."""
        self.switch(self.default_profile_name)
        self._overrides = {}
        logger.info("Reset to default profile '%s'",
                    self.default_profile_name)

    def list_profiles(self):
        return {
            name: {
                **profile,
                "active": name == self._active_name,
            }
            for name, profile in self.profiles.items()
        }
