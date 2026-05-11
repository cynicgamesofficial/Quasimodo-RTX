"""Merge read/write for ``user_data/app_settings.json`` (ignored by git)."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict

from repo_paths import app_settings_path

# Keys safe to restore on startup without overriding engine state from ``q2config.cfg``.
APP_SETTINGS_PATH_KEYS = ("exe", "baseq2", "last_map")


def load_app_settings_paths_only() -> Dict[str, Any]:
    """Subset of app settings: launcher paths and last map only (no engine / launch UI snapshot)."""
    full = load_app_settings_dict()
    return {k: full[k] for k in APP_SETTINGS_PATH_KEYS if k in full}


def load_app_settings_dict() -> Dict[str, Any]:
    p = app_settings_path()
    if not p.is_file():
        return {}
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {}


def save_app_settings_merge(updates: Dict[str, Any]) -> None:
    data = load_app_settings_dict()
    data.update(updates)
    p = app_settings_path()
    try:
        p.write_text(json.dumps(data, indent=2), encoding="utf-8")
    except OSError:
        pass
