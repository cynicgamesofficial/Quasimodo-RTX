# core/compiler.py — CompileRunner for Map Wizard (Quasimodo Wizard)
from PySide6.QtCore import QThread, Signal
import subprocess
import os
from typing import List, Tuple, Optional

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
