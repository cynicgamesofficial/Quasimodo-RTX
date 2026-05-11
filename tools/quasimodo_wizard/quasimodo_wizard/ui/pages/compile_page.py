# compile_page.py — Compile Map page (Map Wizard / Quasimodo Wizard)
from __future__ import annotations

import os
import subprocess
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from core.compiler import QuasimodoCompileRunner
from repo_paths import repository_root, wizard_compile_map_script_path, wizard_q2tool_path

# Display-only repo-relative paths (stable labels in README / docs).
Q220_TOOL_REL = "tools/quasimodo_wizard/quasimodo_wizard/compilers/Q220/q2tool.exe"
Q220_BAT_REL = "tools/quasimodo_wizard/quasimodo_wizard/compilers/Q220/compile_map.bat"


class CompilePage(QWidget):
    """Quasimodo-only compile: bundled ``compilers/Q220`` q2tool + ``compile_map.bat`` (BSP / VIS / RAD)."""

    def __init__(self, main_window):
        super().__init__()
        self.main_window = main_window
        self.runner: QuasimodoCompileRunner | None = None
        root = QVBoxLayout(self)

        title = QLabel("Compile Map — Quasimodo q2tool")
        f = title.font()
        f.setBold(True)
        title.setFont(f)
        root.addWidget(title)

        map_box = QGroupBox("Map source")
        map_form = QFormLayout()
        self.map_edit = QLineEdit()
        self.map_edit.setReadOnly(True)
        self.map_edit.setPlaceholderText("No .map selected — use Browse or the Select Map page")
        btn_browse = QPushButton("Browse .map…")
        btn_browse.clicked.connect(self.browse_map)
        row_map = QHBoxLayout()
        row_map.addWidget(self.map_edit, stretch=1)
        row_map.addWidget(btn_browse)
        w_map = QWidget()
        w_map.setLayout(row_map)
        map_form.addRow("Selected .map:", w_map)
        map_box.setLayout(map_form)
        root.addWidget(map_box)

        tool_box = QGroupBox("Bundled compiler (Q220)")
        tl = QVBoxLayout()
        self.lbl_tool_path = QLabel(f"q2tool: {Q220_TOOL_REL}")
        self.lbl_script_path = QLabel(f"Script: {Q220_BAT_REL}")
        self.lbl_compiler_status = QLabel("Compiler status: —")
        self.lbl_script_status = QLabel("Script status: —")
        for w in (self.lbl_tool_path, self.lbl_script_path, self.lbl_compiler_status, self.lbl_script_status):
            w.setWordWrap(True)
            w.setTextInteractionFlags(Qt.TextSelectableByMouse)
        tl.addWidget(self.lbl_tool_path)
        tl.addWidget(self.lbl_script_path)
        tl.addWidget(self.lbl_compiler_status)
        tl.addWidget(self.lbl_script_status)
        hint = QLabel(
            "BSP / VIS / RAD run via compile_map.bat. Output .bsp is written next to the .map (same folder)."
        )
        hint.setWordWrap(True)
        tl.addWidget(hint)
        tool_box.setLayout(tl)
        root.addWidget(tool_box)

        row_btn = QHBoxLayout()
        self.compile_btn = QPushButton("Compile selected .map")
        self.compile_btn.clicked.connect(self.start_compile)
        self.open_map_folder_btn = QPushButton("Open map folder")
        self.open_map_folder_btn.clicked.connect(self.open_map_folder)
        self.open_output_folder_btn = QPushButton("Open output folder")
        self.open_output_folder_btn.clicked.connect(self.open_output_folder)
        row_btn.addWidget(self.compile_btn)
        row_btn.addWidget(self.open_map_folder_btn)
        row_btn.addWidget(self.open_output_folder_btn)
        root.addLayout(row_btn)

        self.log_box = QPlainTextEdit()
        self.log_box.setReadOnly(True)
        self.log_box.setMinimumHeight(220)
        root.addWidget(self.log_box)

        self.status_label = QLabel("Idle")
        root.addWidget(self.status_label)

        self.update_page()

    def _map_path(self) -> Path | None:
        raw = self.main_window.state.get("selected_path")
        if not raw:
            return None
        return Path(raw)

    def browse_map(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select .map for compile",
            str(repository_root()),
            "Quake II map (*.map)",
        )
        if path:
            self.main_window.state["selected_path"] = path
            self.update_page()

    def _refresh_compiler_status(self) -> None:
        tool = wizard_q2tool_path()
        script = wizard_compile_map_script_path()
        t_ok = tool.is_file()
        s_ok = script.is_file()
        self.lbl_compiler_status.setText(
            "Compiler status: " + ("ready (q2tool.exe found)" if t_ok else "MISSING q2tool.exe under Q220/")
        )
        self.lbl_script_status.setText(
            "Script status: " + ("ready (compile_map.bat found)" if s_ok else "MISSING compile_map.bat under Q220/")
        )

    def update_page(self) -> None:
        self._refresh_compiler_status()
        mp = self._map_path()
        if mp:
            try:
                rel = str(mp.resolve().relative_to(repository_root().resolve()))
            except ValueError:
                rel = str(mp.resolve())
            self.map_edit.setText(rel)
        else:
            self.map_edit.clear()

        tool_ok = wizard_q2tool_path().is_file()
        script_ok = wizard_compile_map_script_path().is_file()
        can_compile = mp is not None and mp.is_file() and mp.suffix.lower() == ".map" and tool_ok and script_ok
        if self.runner and self.runner.isRunning():
            can_compile = False
        self.compile_btn.setEnabled(can_compile)
        have_map = mp is not None
        self.open_map_folder_btn.setEnabled(have_map)
        self.open_output_folder_btn.setEnabled(have_map)

    def _open_dir(self, folder: Path) -> None:
        folder.mkdir(parents=True, exist_ok=True)
        fp = str(folder.resolve())
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        try:
            os.startfile(fp)  # type: ignore[attr-defined]
        except AttributeError:
            if os.name == "nt":
                subprocess.Popen(["explorer", fp], creationflags=flags)
            else:
                subprocess.Popen(["xdg-open", fp])

    def open_map_folder(self) -> None:
        mp = self._map_path()
        if mp and mp.is_file():
            self._open_dir(mp.parent)

    def open_output_folder(self) -> None:
        # BSP is emitted next to the source .map — same folder as “map folder”.
        self.open_map_folder()

    def start_compile(self) -> None:
        mp = self._map_path()
        if not mp or not mp.is_file():
            QMessageBox.warning(self, "No Map Selected", "Select a .map file (Browse or Select Map page).")
            return
        if mp.suffix.lower() != ".map":
            QMessageBox.warning(self, "Invalid Map", "Expected a .map file.")
            return
        if not wizard_compile_map_script_path().is_file():
            QMessageBox.critical(self, "Missing script", f"Bundled script not found:\n{wizard_compile_map_script_path()}")
            return
        if not wizard_q2tool_path().is_file():
            QMessageBox.critical(self, "Missing q2tool", f"Bundled compiler not found:\n{wizard_q2tool_path()}")
            return

        self.log_box.clear()
        self.status_label.setText("Starting…")
        self.compile_btn.setEnabled(False)
        self.runner = QuasimodoCompileRunner(mp.resolve())
        self.runner.log_line.connect(self.append_log)
        self.runner.stage_changed.connect(self.set_status)
        self.runner.finished_ok.connect(self.compile_done)
        self.runner.start()

    def append_log(self, line: str) -> None:
        self.log_box.appendPlainText(line.rstrip("\r\n"))

    def set_status(self, status: str) -> None:
        self.status_label.setText(status)

    def compile_done(self, ok: bool) -> None:
        self.status_label.setText("Done ✓" if ok else "Failed ✗")
        self.compile_btn.setEnabled(True)
        self.update_page()
