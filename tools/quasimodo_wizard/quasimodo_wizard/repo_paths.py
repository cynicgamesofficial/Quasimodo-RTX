"""
Path helpers for Quasimodo Wizard. All roots are derived from source layout — no committed machine paths.
"""
from __future__ import annotations

from pathlib import Path


def package_root() -> Path:
    """Directory of the ``quasimodo_wizard`` Python package (contains ``ui/``, ``core/``)."""
    return Path(__file__).resolve().parent


def tool_root() -> Path:
    """``tools/quasimodo_wizard`` — contains ``main.py``, ``quasimodo_wizard/``, ``user_data/``."""
    return package_root().parent


def repository_root() -> Path:
    """Quasimodo RTX repository root (parent of ``tools/``)."""
    return tool_root().parent


def user_data_dir() -> Path:
    """Writable per-user JSON and presets (gitignored patterns recommended for secrets)."""
    p = tool_root() / "user_data"
    p.mkdir(parents=True, exist_ok=True)
    return p


def presets_dir() -> Path:
    p = user_data_dir() / "presets"
    p.mkdir(parents=True, exist_ok=True)
    return p


def app_settings_path() -> Path:
    return user_data_dir() / "app_settings.json"


def default_presets_path() -> Path:
    """Shipped template presets (relative paths only)."""
    return tool_root() / "presets" / "default_presets.json"
