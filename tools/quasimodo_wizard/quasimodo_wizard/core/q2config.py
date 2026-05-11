"""
Read-only parser for Quake II / Q2RTX ``baseq2/q2config.cfg``-style lines.

Does not write config files. Returns last assignment per cvar (later ``set`` / ``seta`` / ``sets`` wins).

Uses manual tokenization (no ``shlex``) so PyInstaller one-file builds do not depend on stdlib
hooks for ``shlex`` on Python 3.14+.
"""
from __future__ import annotations

import re
from pathlib import Path
from typing import Dict

_SET_PREFIX = re.compile(r"^\s*(?:set|seta|sets)\s+", re.IGNORECASE)


def get_q2config_path(*, repo_root: Path | None = None) -> Path:
    """Return ``{repo}/baseq2/q2config.cfg``. ``repo_root`` defaults to ``repository_root()``."""
    if repo_root is None:
        from repo_paths import repository_root

        repo_root = repository_root()
    return (repo_root / "baseq2" / "q2config.cfg").resolve()


def _strip_comment(line: str) -> str:
    if "//" in line:
        line = line.split("//", 1)[0]
    i = line.find("#")
    if i >= 0 and (i == 0 or line[i - 1].isspace()):
        line = line[:i]
    return line.rstrip()


def _parse_value_suffix(val_rest: str) -> str:
    """Value is remainder after cvar name (quoted one token or unquoted rest of line)."""
    vr = val_rest.strip()
    if not vr:
        return ""
    if vr[0] == '"':
        end = 1
        while end < len(vr):
            if vr[end] == '"' and vr[end - 1] != "\\":
                return vr[1:end]
            end += 1
        return vr[1:]
    return vr


def _parse_first_token(rest: str) -> tuple[str, str] | None:
    """Split ``name`` and remainder after name; name may be quoted."""
    rest = rest.lstrip()
    if not rest:
        return None
    if rest[0] == '"':
        i = 1
        while i < len(rest):
            if rest[i] == '"' and rest[i - 1] != "\\":
                name = rest[1:i]
                return name, rest[i + 1 :]
            i += 1
        return None
    parts = rest.split(None, 1)
    name = parts[0]
    tail = parts[1] if len(parts) > 1 else ""
    return name, tail


def _parse_set_line(line: str) -> tuple[str, str] | None:
    """Parse ``set`` / ``seta`` / ``sets`` lines (quoted or unquoted cvar names and values)."""
    line = line.strip()
    if not line:
        return None
    m = _SET_PREFIX.match(line)
    if not m:
        return None
    rest = line[m.end() :]
    parsed = _parse_first_token(rest)
    if not parsed:
        return None
    name, val_rest = parsed
    if not name:
        return None
    val = _parse_value_suffix(val_rest)
    return name.lower(), val


def parse_q2config_text(text: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for raw in text.splitlines():
        line = _strip_comment(raw)
        if not line.strip():
            continue
        parsed = _parse_set_line(line)
        if not parsed:
            continue
        name, value = parsed
        out[name.lower()] = value
    return out


def parse_q2config_file(path: Path) -> Dict[str, str]:
    if not path.is_file():
        return {}
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    return parse_q2config_text(text)


def load_q2config(path: Path | None = None, *, repo_root: Path | None = None) -> Dict[str, str]:
    """
    Load cvars from ``q2config.cfg``.

    If ``path`` is given, read that file. Otherwise ``get_q2config_path(repo_root=repo_root)``.
    """
    cfg = path if path is not None else get_q2config_path(repo_root=repo_root)
    return parse_q2config_file(cfg)
