# core/compiler.py — CompileRunner for Map Wizard (Quasimodo Wizard)
from __future__ import annotations

from PySide6.QtCore import QThread, Signal
import os
import subprocess
from pathlib import Path
from typing import Callable, List, Optional, Tuple

from repo_paths import repository_root, wizard_compile_map_script_path, wizard_q2tool_path


def compile_quasimodo_map(
    map_path: Path,
    on_output: Optional[Callable[[str], None]] = None,
) -> int:
    """
    Run bundled ``compilers/Q220/compile_map.bat`` with one ``.map`` (Quasimodo q2tool BSP + VIS + RAD).

    Uses ``cmd /c`` with an argument list (no shell string concatenation). Sets
    ``QUASIMODO_WIZARD_COMPILE=1`` so the batch skips interactive ``pause``, and passes
    ``QUASIMODO_GAMEDIR`` / ``QUASIMODO_BASEDIR`` for packaged or non-standard layouts.

    Returns the batch script's exit code (0 = success).
    """
    map_path = Path(map_path).resolve()
    if not map_path.is_file():
        if on_output:
            on_output(f"Map file not found: {map_path}\n")
        return 1
    if map_path.suffix.lower() != ".map":
        if on_output:
            on_output("Expected a .map file.\n")
        return 1

    script = wizard_compile_map_script_path()
    tool = wizard_q2tool_path()
    if not script.is_file():
        if on_output:
            on_output(f"Missing compile script: {script}\n")
        return 1
    if not tool.is_file():
        if on_output:
            on_output(f"Missing compiler: {tool}\n")
            on_output("Expected bundled q2tool.exe under quasimodo_wizard/compilers/Q220/.\n")
        return 1

    repo = repository_root().resolve()
    baseq2 = repo / "baseq2"
    cmd_exe = os.environ.get("COMSPEC") or os.environ.get("SystemRoot", "") + r"\System32\cmd.exe"
    if not cmd_exe or not Path(cmd_exe).is_file():
        cmd_exe = "cmd.exe"

    cmd = [cmd_exe, "/c", str(script), str(map_path)]
    env = os.environ.copy()
    env["QUASIMODO_WIZARD_COMPILE"] = "1"
    env["QUASIMODO_GAMEDIR"] = str(repo)
    env["QUASIMODO_BASEDIR"] = str(baseq2)

    creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    try:
        proc = subprocess.Popen(
            cmd,
            cwd=str(repo),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=env,
            creationflags=creationflags,
        )
    except OSError as e:
        if on_output:
            on_output(f"Failed to start compile: {e}\n")
        return 1

    assert proc.stdout is not None
    for line in proc.stdout:
        if on_output:
            on_output(line)
    proc.wait()
    return int(proc.returncode or 0)


class QuasimodoCompileRunner(QThread):
    """Runs bundled ``compilers/Q220/compile_map.bat`` for the selected .map; streams stdout to the UI log."""

    log_line = Signal(str)
    stage_changed = Signal(str)
    finished_ok = Signal(bool)

    def __init__(self, map_path: Path, parent=None):
        super().__init__(parent)
        self.map_path = Path(map_path)

    def run(self) -> None:
        self.stage_changed.emit("Running Q220 compile_map.bat…")
        self.log_line.emit("=== Compile Map — Quasimodo q2tool (compilers/Q220) ===\n")

        def _emit(line: str) -> None:
            self.log_line.emit(line.rstrip("\r\n") + "\n")

        rc = compile_quasimodo_map(self.map_path, on_output=_emit)
        if rc == 0:
            self.stage_changed.emit("Done ✓")
            self.finished_ok.emit(True)
        else:
            self.log_line.emit(f"compile_map.bat exited with code {rc}\n")
            self.stage_changed.emit("Failed ✗")
            self.finished_ok.emit(False)


class CompileRunner(QThread):
    log_line = Signal(str)
    stage_changed = Signal(str)
    finished_ok = Signal(bool)

    def __init__(self, pipeline: List[Tuple[str, List[str]]], parent=None):
        super().__init__(parent)
        self.pipeline = pipeline
        self._stopped = False

    def run(self):
        for idx, (tool_path, args) in enumerate(self.pipeline):
            tool_name = os.path.basename(tool_path).upper()
            self.stage_changed.emit(f"Running {tool_name}…")
            self.log_line.emit(f"=== {tool_name} ===\n")
            try:
                proc = subprocess.Popen(
                    [tool_path] + args,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
            except Exception as e:
                self.log_line.emit(f"Failed to start {tool_name}: {e}\n")
                self.stage_changed.emit("Failed ✗")
                self.finished_ok.emit(False)
                return
            # Stream output
            assert proc.stdout is not None
            for line in proc.stdout:
                self.log_line.emit(line.rstrip("\r\n") + "\n")
            proc.wait()
            if proc.returncode != 0:
                self.log_line.emit(f"{tool_name} failed with exit code {proc.returncode}\n")
                self.stage_changed.emit("Failed ✗")
                self.finished_ok.emit(False)
                return
        self.stage_changed.emit("Done ✓")
        self.finished_ok.emit(True)
