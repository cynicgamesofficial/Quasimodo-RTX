"""
Path helpers for Quasimodo Wizard. All roots are derived from source layout — no committed machine paths.
"""
from __future__ import annotations

import sys
from pathlib import Path


def package_root() -> Path:
    """Directory of the ``quasimodo_wizard`` Python package (contains ``ui/``, ``core/``)."""
    return Path(__file__).resolve().parent


def tool_root() -> Path:
    """``tools/quasimodo_wizard`` — contains ``main.py``, ``quasimodo_wizard/``, ``user_data/``."""
    return package_root().parent


def _repository_root_from_source_tree() -> Path:
    return tool_root().parent.parent


def _repository_root_frozen() -> Path:
    """Best-effort repo root when running as PyInstaller one-file (launcher next to clone)."""
    candidates = (
        Path(sys.executable).resolve().parent,
        Path.cwd().resolve(),
    )
    for start in candidates:
        p = start
        for _ in range(14):
            if (p / "baseq2").is_dir() and (
                (p / "CMakeLists.txt").is_file() or (p / "q2rtx.exe").is_file()
            ):
                return p
            parent = p.parent
            if parent == p:
                break
            p = parent
    return Path.cwd().resolve()


def repository_root() -> Path:
    """Quasimodo RTX repository root (contains ``tools/``, ``baseq2/``, ``editor/``)."""
    if getattr(sys, "frozen", False):
        return _repository_root_frozen()
    return _repository_root_from_source_tree()


def wizard_compiler_dir() -> Path:
    """Bundled Quasimodo q2tool folder: ``quasimodo_wizard/compilers/Q220`` (next to ``q2tool.exe``)."""
    return package_root() / "compilers" / "Q220"


def wizard_q2tool_path() -> Path:
    return wizard_compiler_dir() / "q2tool.exe"


def wizard_compile_map_script_path() -> Path:
    return wizard_compiler_dir() / "compile_map.bat"


def editor_dir() -> Path:
    """Shipped map compiler folder at repository root: ``editor/`` (``q2tool.exe``, ``compile_map.bat``)."""
    return repository_root() / "editor"


def q2tool_path() -> Path:
    """``editor/q2tool.exe`` — Quasimodo / Q2 BSP toolchain."""
    return editor_dir() / "q2tool.exe"


def compile_map_script_path() -> Path:
    """``editor/compile_map.bat`` — BSP / VIS / RAD driver script next to ``q2tool``."""
    return editor_dir() / "compile_map.bat"


def user_data_dir() -> Path:
    """
    Writable per-user JSON and presets (gitignored patterns recommended for secrets).

    In PyInstaller one-file mode, ``tool_root()`` is ``sys._MEIPASS`` (extracted temp). Writing
    ``user_data`` there can fail (read-only, AV) or be wiped on exit. Use the real repo's
    ``tools/quasimodo_wizard/user_data`` when ``sys.frozen``, with a local-app fallback.
    """
    if getattr(sys, "frozen", False):
        try:
            p = repository_root() / "tools" / "quasimodo_wizard" / "user_data"
            p.mkdir(parents=True, exist_ok=True)
            return p
        except OSError:
            import os

            base = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "QuasimodoRTX" / "Wizard"
            base.mkdir(parents=True, exist_ok=True)
            return base
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


# Repo-relative default for compiled BSP copy destination (no absolute paths in committed defaults).
DEFAULT_COMPILED_MAP_DESTINATION_REL = "baseq2/maps"


def wizard_asset_path(*relative_parts: str) -> Path:
    """Path under ``quasimodo_wizard/assets/`` (works in source tree and PyInstaller extract)."""
    if not relative_parts:
        return package_root() / "assets"
    return package_root() / "assets" / Path(*relative_parts)
