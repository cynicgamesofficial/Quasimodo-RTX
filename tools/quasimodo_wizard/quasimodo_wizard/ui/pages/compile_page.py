# compile_page.py — Compile Map page (Map Wizard / Quasimodo Wizard)
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QComboBox, QRadioButton,
    QButtonGroup, QGroupBox, QCheckBox, QLineEdit, QLabel, QFileDialog, QPlainTextEdit, QMessageBox
)
from PySide6.QtCore import Qt
from pathlib import Path
import os
from core.compiler import CompileRunner
from typing import Optional

# Constants for tool paths
COMPILER_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "compilers", "Q2"))
QBSP_EXE = os.path.join(COMPILER_DIR, "qbsp.exe")
VIS_EXE = os.path.join(COMPILER_DIR, "vis.exe")
LIGHT_EXE = os.path.join(COMPILER_DIR, "light.exe")

class CompilePage(QWidget):
    def __init__(self, main_window):
        super().__init__()
        self.main_window = main_window
        self.runner: Optional[CompileRunner] = None
        self.setLayout(QVBoxLayout())
        layout = self.layout()

        # Compile mode selector
        self.mode_group = QButtonGroup(self)
        mode_box = QGroupBox("Compile Mode")
        mode_layout = QVBoxLayout()
        self.rb_bsp = QRadioButton("BSP Only")
        self.rb_vis = QRadioButton("BSP + VIS (fast)")
        self.rb_full = QRadioButton("Full (BSP + VIS + LIGHT)")
        self.rb_bsp.setChecked(True)
        self.mode_group.addButton(self.rb_bsp, 0)
        self.mode_group.addButton(self.rb_vis, 1)
        self.mode_group.addButton(self.rb_full, 2)
        mode_layout.addWidget(self.rb_bsp)
        mode_layout.addWidget(self.rb_vis)
        mode_layout.addWidget(self.rb_full)
        mode_box.setLayout(mode_layout)
        layout.addWidget(mode_box)

        # Advanced options
        self.adv_box = QGroupBox("Advanced Options")
        self.adv_box.setCheckable(True)
        self.adv_box.setChecked(False)
        adv_layout = QVBoxLayout()
        self.cb_verbose = QCheckBox("-verbose (detailed log)")
        self.cb_novis = QCheckBox("-novis (skip VIS)")
        self.cb_extra = QCheckBox("-extra (high quality light)")
        self.cb_bounce = QCheckBox("-bounce (radiosity)")
        adv_layout.addWidget(self.cb_verbose)
        adv_layout.addWidget(self.cb_novis)
        adv_layout.addWidget(self.cb_extra)
        adv_layout.addWidget(self.cb_bounce)
        self.adv_box.setLayout(adv_layout)
        layout.addWidget(self.adv_box)

        # Output folder selection
        out_layout = QHBoxLayout()
        self.out_label = QLabel("Output Folder:")
        self.out_path = QLineEdit()
        self.out_path.setReadOnly(True)
        self.browse_btn = QPushButton("Browse")
        self.browse_btn.clicked.connect(self.select_output_folder)
        out_layout.addWidget(self.out_label)
        out_layout.addWidget(self.out_path)
        out_layout.addWidget(self.browse_btn)
        layout.addLayout(out_layout)

        # Compile button
        self.compile_btn = QPushButton("Compile")
        self.compile_btn.clicked.connect(self.start_compile)
        self.compile_btn.setEnabled(False)
        layout.addWidget(self.compile_btn)

        # Log output
        self.log_box = QPlainTextEdit()
        self.log_box.setReadOnly(True)
        self.log_box.setMinimumHeight(200)
        layout.addWidget(self.log_box)

        # Status label
        self.status_label = QLabel("Idle")
        layout.addWidget(self.status_label)

        self.update_page()

    def update_page(self):
        """Update output folder and button state."""
        state = self.main_window.state
        map_path = state.get("selected_path")
        if map_path:
            default_out = os.path.dirname(map_path)
            if not state.get("compile_output_folder"):
                state["compile_output_folder"] = default_out
            self.out_path.setText(state["compile_output_folder"])
            self.compile_btn.setEnabled(True)
        else:
            self.out_path.setText("")
            self.compile_btn.setEnabled(False)
        if self.runner and self.runner.isRunning():
            self.compile_btn.setEnabled(False)

    def select_output_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Select Output Folder")
        if folder:
            self.main_window.state["compile_output_folder"] = folder
            self.out_path.setText(folder)

    def start_compile(self):
        state = self.main_window.state
        map_path = state.get("selected_path")
        out_folder = state.get("compile_output_folder")
        if not map_path:
            QMessageBox.warning(self, "No Map Selected", "Please select a .map file first.")
            return
        if not out_folder:
            QMessageBox.warning(self, "No Output Folder", "Please select an output folder.")
            return
        # Check compilers
        missing = [exe for exe in [QBSP_EXE, VIS_EXE, LIGHT_EXE] if not os.path.isfile(exe)]
        if missing:
            QMessageBox.critical(self, "Compiler Missing", f"Missing compiler(s):\n" + "\n".join(missing))
            return
        # Build pipeline
        map_path = os.path.abspath(map_path)
        out_folder = os.path.abspath(out_folder)
        map_name = os.path.splitext(os.path.basename(map_path))[0]
        bsp_path = os.path.join(out_folder, map_name + ".bsp")
        mode = self.mode_group.checkedId()
        adv = self.adv_box.isChecked()
        verbose = adv and self.cb_verbose.isChecked()
        novis = adv and self.cb_novis.isChecked()
        extra = adv and self.cb_extra.isChecked()
        bounce = adv and self.cb_bounce.isChecked()
        pipeline = []
        # QBSP
        qbsp_args = ["-q2bsp"]
        if verbose:
            qbsp_args.append("-verbose")
        qbsp_args += [map_path, bsp_path]
        pipeline.append((QBSP_EXE, qbsp_args))
        # VIS
        if not novis and mode in (1, 2):
            vis_args = []
            if verbose:
                vis_args.append("-verbose")
            if mode == 1:
                vis_args.append("-fast")
            vis_args.append(bsp_path)
            pipeline.append((VIS_EXE, vis_args))
        # LIGHT
        if mode == 2:
            light_args = []
            if verbose:
                light_args.append("-verbose")
            if extra:
                light_args.append("-extra")
            if bounce:
                light_args += ["-bounce", "1"]
            light_args.append(bsp_path)
            pipeline.append((LIGHT_EXE, light_args))
        # Start compile
        self.log_box.clear()
        self.status_label.setText("Running QBSP…")
        self.compile_btn.setEnabled(False)
        self.runner = CompileRunner(pipeline)
        self.runner.log_line.connect(self.append_log)
        self.runner.stage_changed.connect(self.set_status)
        self.runner.finished_ok.connect(self.compile_done)
        self.runner.start()

    def append_log(self, line: str):
        self.log_box.appendPlainText(line.rstrip("\r\n"))

    def set_status(self, status: str):
        self.status_label.setText(status)

    def compile_done(self, ok: bool):
        if ok:
            self.status_label.setText("Done ✓")
        else:
            self.status_label.setText("Failed ✗")
        self.compile_btn.setEnabled(True)
