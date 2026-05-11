"""
Launch tab — run ``q2rtx.exe`` with common +set options and presets (no gameplay logic).
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QFormLayout,
    QGroupBox,
    QLabel,
    QPushButton,
    QLineEdit,
    QComboBox,
    QSpinBox,
    QCheckBox,
    QFileDialog,
    QMessageBox,
    QPlainTextEdit,
)

from repo_paths import (
    app_settings_path,
    default_presets_path,
    presets_dir,
    repository_root,
)


def _safe_folder_open(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    if sys.platform == "win32":
        os.startfile(path)  # nosec: trusted user action opening repo-relative folder
    else:
        subprocess.Popen(["xdg-open", str(path)])  # nosec


class LaunchTab(QWidget):
    def __init__(self, main_window):
        super().__init__()
        self.main_window = main_window
        self._repo = repository_root()
        root = QVBoxLayout(self)

        paths = QGroupBox("Executable / game data")
        pl = QFormLayout(paths)
        self.exe_edit = QLineEdit()
        self.exe_edit.setPlaceholderText("q2rtx.exe (relative to repo root, or browse for absolute)")
        self._btn_exe = QPushButton("Browse…")
        self._btn_exe.clicked.connect(self._browse_exe)
        row_exe = QHBoxLayout()
        row_exe.addWidget(self.exe_edit)
        row_exe.addWidget(self._btn_exe)
        pl.addRow("q2rtx.exe:", row_exe)
        self.baseq2_edit = QLineEdit()
        self.baseq2_edit.setPlaceholderText("baseq2 (relative to repo root)")
        self._btn_base = QPushButton("Browse…")
        self._btn_base.clicked.connect(self._browse_baseq2)
        row_b = QHBoxLayout()
        row_b.addWidget(self.baseq2_edit)
        row_b.addWidget(self._btn_base)
        pl.addRow("baseq2 folder:", row_b)
        root.addWidget(paths)

        mapg = QGroupBox("Map")
        ml = QFormLayout(mapg)
        self.map_combo = QComboBox()
        self.map_combo.setEditable(False)
        self.map_combo.currentTextChanged.connect(self._update_preview)
        self.map_manual = QLineEdit()
        self.map_manual.setPlaceholderText("Override map name (no .bsp), e.g. jungletest_small")
        self.map_manual.textChanged.connect(self._update_preview)
        ml.addRow("Installed .bsp:", self.map_combo)
        ml.addRow("Manual map name:", self.map_manual)
        root.addWidget(mapg)

        vid = QGroupBox("Video")
        vl = QFormLayout(vid)
        self.res_preset = QComboBox()
        self.res_preset.addItems(["1280x720", "1920x1080", "2560x1440", "3840x2160", "Custom"])
        self.res_preset.setCurrentText("1920x1080")
        self.res_preset.currentTextChanged.connect(self._on_preset_resolution)
        self.w_spin = QSpinBox()
        self.w_spin.setRange(640, 16384)
        self.w_spin.setValue(1920)
        self.h_spin = QSpinBox()
        self.h_spin.setRange(480, 16384)
        self.h_spin.setValue(1080)
        self.w_spin.valueChanged.connect(self._update_preview)
        self.h_spin.valueChanged.connect(self._update_preview)
        self.fullscreen = QComboBox()
        self.fullscreen.addItems(["Windowed (vid_fullscreen 0)", "Fullscreen (1)"])
        self.fullscreen.currentIndexChanged.connect(self._update_preview)
        vl.addRow("Resolution preset:", self.res_preset)
        vl.addRow("Width:", self.w_spin)
        vl.addRow("Height:", self.h_spin)
        vl.addRow("Display mode:", self.fullscreen)
        self.pt_dlss = QComboBox()
        self.pt_dlss.addItems(["(default — omit)", "Off (+set pt_dlss 0)", "On (+set pt_dlss 1)"])
        self.pt_dlss.currentIndexChanged.connect(self._update_preview)
        vl.addRow("DLSS hint (pt_dlss):", self.pt_dlss)
        root.addWidget(vid)

        ter = QGroupBox("Terrain / RTX / UI")
        tl = QFormLayout(ter)
        self.terrain_enable = QCheckBox("terrain_enable 1")
        self.terrain_enable.setChecked(True)
        self.terrain_collision = QCheckBox("terrain_collision 1")
        self.terrain_collision.setChecked(True)
        self.terrain_water = QCheckBox("terrain_water 1")
        self.terrain_water.setChecked(True)
        self.terrain_rtx = QCheckBox("terrain_rtx_instance 1")
        self.terrain_rtx.setChecked(True)
        self.jolt_compare = QCheckBox("terrain_collision_backend 1 (Jolt compare — diagnostic only)")
        self.jolt_compare.setChecked(False)
        self.ui_splash = QCheckBox("ui_splash 1 (splash on)")
        self.ui_splash.setChecked(False)
        self.logfile = QCheckBox("logfile 2 + logfile_flush 1")
        self.logfile.setChecked(True)
        self.developer = QCheckBox("developer 1")
        self.developer.setChecked(False)
        for w in (
            self.terrain_enable,
            self.terrain_collision,
            self.terrain_water,
            self.terrain_rtx,
            self.jolt_compare,
            self.ui_splash,
            self.logfile,
            self.developer,
        ):
            w.stateChanged.connect(lambda *_: self._update_preview())
        tl.addRow(self.terrain_enable)
        tl.addRow(self.terrain_collision)
        tl.addRow(self.terrain_water)
        tl.addRow(self.terrain_rtx)
        tl.addRow(self.jolt_compare)
        tl.addRow(self.ui_splash)
        tl.addRow(self.logfile)
        tl.addRow(self.developer)
        root.addWidget(ter)

        ded = QHBoxLayout()
        self.dedicated = QCheckBox("+set dedicated 1 (listen server)")
        self.dedicated.stateChanged.connect(lambda *_: self._update_preview())
        ded.addWidget(self.dedicated)
        ded.addStretch()
        root.addLayout(ded)

        pre = QGroupBox("Presets")
        pr = QHBoxLayout(pre)
        self.preset_combo = QComboBox()
        self.preset_combo.setEditable(False)
        self.preset_combo.currentTextChanged.connect(self._on_preset_selected)
        self.preset_name = QLineEdit()
        self.preset_name.setPlaceholderText("preset name")
        self._btn_psave = QPushButton("Save preset")
        self._btn_psave.clicked.connect(self._preset_save)
        self._btn_pdel = QPushButton("Delete preset")
        self._btn_pdel.clicked.connect(self._preset_delete)
        pr.addWidget(QLabel("User presets:"))
        pr.addWidget(self.preset_combo, stretch=1)
        pr.addWidget(self.preset_name)
        pr.addWidget(self._btn_psave)
        pr.addWidget(self._btn_pdel)
        root.addWidget(pre)

        act = QHBoxLayout()
        self._btn_logs = QPushButton("Open logs folder")
        self._btn_logs.clicked.connect(self._open_logs)
        self._btn_maps = QPushButton("Open baseq2/maps")
        self._btn_maps.clicked.connect(self._open_maps)
        self._btn_copy = QPushButton("Copy command")
        self._btn_copy.clicked.connect(self._copy_cmd)
        self._btn_launch = QPushButton("Launch q2rtx.exe")
        self._btn_launch.clicked.connect(self._launch)
        act.addWidget(self._btn_logs)
        act.addWidget(self._btn_maps)
        act.addWidget(self._btn_copy)
        act.addWidget(self._btn_launch)
        root.addLayout(act)

        root.addWidget(QLabel("Command preview (repo root is working directory):"))
        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        self.preview.setMaximumBlockCount(200)
        self.preview.setMinimumHeight(100)
        root.addWidget(self.preview)

        self._load_defaults()
        self._load_app_settings()
        if not app_settings_path().is_file():
            self.load_shipped_defaults_if_present()
        self._refresh_map_list()
        self._refresh_preset_list()
        self._on_preset_resolution()
        self._update_preview()

    def _load_defaults(self) -> None:
        self.exe_edit.setText("q2rtx.exe")
        self.baseq2_edit.setText("baseq2")
        p = self._repo / "q2rtx.exe"
        if not p.is_file():
            self.exe_edit.clear()
            self.exe_edit.setPlaceholderText("q2rtx.exe not found in repo root — browse…")

    def _resolve_exe(self) -> Optional[Path]:
        raw = self.exe_edit.text().strip()
        if not raw:
            return None
        p = Path(raw)
        if p.is_file():
            return p
        cand = (self._repo / raw).resolve()
        if cand.is_file():
            return cand
        return None

    def _browse_exe(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Select q2rtx.exe", str(self._repo), "Executable (q2rtx.exe);;All (*.*)"
        )
        if path:
            try:
                rel = os.path.relpath(path, str(self._repo))
                if not rel.startswith(".."):
                    self.exe_edit.setText(rel.replace("\\", "/"))
                else:
                    self.exe_edit.setText(path.replace("\\", "/"))
            except ValueError:
                self.exe_edit.setText(path.replace("\\", "/"))
            self._save_app_settings()
            self._update_preview()

    def _browse_baseq2(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "Select baseq2 folder", str(self._repo))
        if path:
            try:
                rel = os.path.relpath(path, str(self._repo))
                if not rel.startswith(".."):
                    self.baseq2_edit.setText(rel.replace("\\", "/"))
                else:
                    self.baseq2_edit.setText(path.replace("\\", "/"))
            except ValueError:
                self.baseq2_edit.setText(path.replace("\\", "/"))
            self._save_app_settings()
            self._refresh_map_list()
            self._update_preview()

    def _baseq2_path(self) -> Path:
        raw = self.baseq2_edit.text().strip() or "baseq2"
        p = Path(raw)
        if p.is_dir():
            return p.resolve()
        return (self._repo / raw).resolve()

    def _refresh_map_list(self) -> None:
        self.map_combo.blockSignals(True)
        self.map_combo.clear()
        self.map_combo.addItem("(none)")
        maps_dir = self._baseq2_path() / "maps"
        if maps_dir.is_dir():
            for bsp in sorted(maps_dir.glob("*.bsp")):
                self.map_combo.addItem(bsp.stem)
        self.map_combo.blockSignals(False)

    def _on_preset_resolution(self) -> None:
        t = self.res_preset.currentText()
        m = re.match(r"^(\d+)x(\d+)$", t)
        if m:
            self.w_spin.setValue(int(m.group(1)))
            self.h_spin.setValue(int(m.group(2)))
        self._update_preview()

    def _selected_map(self) -> str:
        manual = self.map_manual.text().strip()
        if manual:
            return manual
        t = self.map_combo.currentText().strip()
        if t and t != "(none)":
            return t
        return ""

    def _append_sets(self, acc: List[str], name: str, value: str) -> None:
        acc.extend(["+set", name, value])

    def build_argv(self) -> List[str]:
        exe = self._resolve_exe()
        if not exe:
            return []
        args: List[str] = [str(exe)]
        self._append_sets(args, "vid_width", str(self.w_spin.value()))
        self._append_sets(args, "vid_height", str(self.h_spin.value()))
        self._append_sets(args, "vid_fullscreen", str(self.fullscreen.currentIndex()))
        idx = self.pt_dlss.currentIndex()
        if idx == 1:
            self._append_sets(args, "pt_dlss", "0")
        elif idx == 2:
            self._append_sets(args, "pt_dlss", "1")
        self._append_sets(args, "terrain_enable", "1" if self.terrain_enable.isChecked() else "0")
        self._append_sets(args, "terrain_collision", "1" if self.terrain_collision.isChecked() else "0")
        self._append_sets(args, "terrain_water", "1" if self.terrain_water.isChecked() else "0")
        self._append_sets(args, "terrain_rtx_instance", "1" if self.terrain_rtx.isChecked() else "0")
        self._append_sets(args, "terrain_collision_backend", "1" if self.jolt_compare.isChecked() else "0")
        self._append_sets(args, "ui_splash", "1" if self.ui_splash.isChecked() else "0")
        if self.logfile.isChecked():
            self._append_sets(args, "logfile", "2")
            self._append_sets(args, "logfile_flush", "1")
        self._append_sets(args, "developer", "1" if self.developer.isChecked() else "0")
        if self.dedicated.isChecked():
            self._append_sets(args, "dedicated", "1")
        else:
            self._append_sets(args, "dedicated", "0")
        adv = self.main_window.advanced_tab.get_cvar_pairs()
        for n, v in adv:
            self._append_sets(args, n, v)
        m = self._selected_map()
        if m:
            args.append("+map")
            args.append(m)
        return args

    def _preview_text(self) -> str:
        argv = self.build_argv()
        if not argv:
            return "(Set a valid q2rtx.exe path to generate a command.)"
        parts = []
        for a in argv:
            if " " in a or "\t" in a:
                parts.append('"' + a.replace('"', '\\"') + '"')
            else:
                parts.append(a)
        return " ".join(parts)

    def _update_preview(self) -> None:
        self.preview.setPlainText(self._preview_text())

    def _copy_cmd(self) -> None:
        QGuiApplication.clipboard().setText(self._preview_text())

    def _launch(self) -> None:
        argv = self.build_argv()
        if not argv:
            QMessageBox.warning(self, "Launch", "Could not resolve q2rtx.exe.")
            return
        cwd = str(self._repo)
        try:
            subprocess.Popen(argv, cwd=cwd)  # noqa: S603 — argv list, no shell
        except Exception as e:
            QMessageBox.critical(self, "Launch failed", str(e))

    def _open_logs(self) -> None:
        p = self._baseq2_path() / "logs"
        _safe_folder_open(p)

    def _open_maps(self) -> None:
        p = self._baseq2_path() / "maps"
        _safe_folder_open(p)

    def _save_app_settings(self) -> None:
        data = {
            "exe": self.exe_edit.text().strip(),
            "baseq2": self.baseq2_edit.text().strip(),
            "last_map": self._selected_map(),
            "last_preset": self.preset_combo.currentText(),
        }
        try:
            app_settings_path().write_text(json.dumps(data, indent=2), encoding="utf-8")
        except OSError:
            pass

    def _load_app_settings(self) -> None:
        p = app_settings_path()
        if not p.is_file():
            return
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
            if data.get("exe"):
                self.exe_edit.setText(str(data["exe"]))
            if data.get("baseq2"):
                self.baseq2_edit.setText(str(data["baseq2"]))
            if data.get("last_map"):
                self.map_manual.setText(str(data["last_map"]))
        except (json.JSONDecodeError, OSError):
            pass

    def _refresh_preset_list(self) -> None:
        self.preset_combo.blockSignals(True)
        self.preset_combo.clear()
        self.preset_combo.addItem("(none)")
        for f in sorted(presets_dir().glob("*.json")):
            self.preset_combo.addItem(f.stem)
        self.preset_combo.blockSignals(False)

    def _preset_save(self) -> None:
        name = self.preset_name.text().strip() or "preset"
        safe = re.sub(r"[^\w\-]+", "_", name)
        path = presets_dir() / f"{safe}.json"
        payload = {
            "version": 1,
            "launch": self._gather_launch_dict(),
            "advanced": self.main_window.advanced_tab.get_settings(),
        }
        try:
            path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
            self._refresh_preset_list()
            self.preset_combo.setCurrentText(safe)
            QMessageBox.information(self, "Preset", f"Saved: {path.name}")
        except OSError as e:
            QMessageBox.warning(self, "Save failed", str(e))

    def _preset_delete(self) -> None:
        name = self.preset_combo.currentText()
        if not name or name == "(none)":
            return
        path = presets_dir() / f"{name}.json"
        if path.is_file():
            path.unlink()
        self._refresh_preset_list()

    def _on_preset_selected(self, name: str) -> None:
        if not name or name == "(none)":
            return
        path = presets_dir() / f"{name}.json"
        if not path.is_file():
            return
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            ld = data.get("launch", data)
            if isinstance(ld, dict):
                self._apply_launch_dict(ld)
            adv = data.get("advanced")
            if isinstance(adv, list):
                self.main_window.advanced_tab.set_settings(adv)
            self._update_preview()
        except (json.JSONDecodeError, OSError, KeyError) as e:
            QMessageBox.warning(self, "Preset", str(e))

    def _gather_launch_dict(self) -> Dict[str, Any]:
        return {
            "exe": self.exe_edit.text().strip(),
            "baseq2": self.baseq2_edit.text().strip(),
            "resolution_preset": self.res_preset.currentText(),
            "width": self.w_spin.value(),
            "height": self.h_spin.value(),
            "fullscreen_index": self.fullscreen.currentIndex(),
            "pt_dlss_index": self.pt_dlss.currentIndex(),
            "terrain_enable": self.terrain_enable.isChecked(),
            "terrain_collision": self.terrain_collision.isChecked(),
            "terrain_water": self.terrain_water.isChecked(),
            "terrain_rtx": self.terrain_rtx.isChecked(),
            "jolt_compare": self.jolt_compare.isChecked(),
            "ui_splash": self.ui_splash.isChecked(),
            "logfile": self.logfile.isChecked(),
            "developer": self.developer.isChecked(),
            "dedicated": self.dedicated.isChecked(),
            "map_manual": self.map_manual.text().strip(),
            "map_combo": self.map_combo.currentText(),
        }

    def _apply_launch_dict(self, d: Dict[str, Any]) -> None:
        if "exe" in d:
            self.exe_edit.setText(str(d["exe"]))
        if "baseq2" in d:
            self.baseq2_edit.setText(str(d["baseq2"]))
        if "resolution_preset" in d:
            i = self.res_preset.findText(str(d["resolution_preset"]))
            if i >= 0:
                self.res_preset.setCurrentIndex(i)
        if "width" in d:
            self.w_spin.setValue(int(d["width"]))
        if "height" in d:
            self.h_spin.setValue(int(d["height"]))
        if "fullscreen_index" in d:
            self.fullscreen.setCurrentIndex(int(d["fullscreen_index"]))
        if "pt_dlss_index" in d:
            self.pt_dlss.setCurrentIndex(int(d["pt_dlss_index"]))
        for key, w in (
            ("terrain_enable", self.terrain_enable),
            ("terrain_collision", self.terrain_collision),
            ("terrain_water", self.terrain_water),
            ("terrain_rtx", self.terrain_rtx),
            ("jolt_compare", self.jolt_compare),
            ("ui_splash", self.ui_splash),
            ("logfile", self.logfile),
            ("developer", self.developer),
            ("dedicated", self.dedicated),
        ):
            if key in d:
                w.setChecked(bool(d[key]))
        if "map_manual" in d:
            self.map_manual.setText(str(d.get("map_manual", "")))
        if "map_combo" in d:
            t = str(d["map_combo"])
            i = self.map_combo.findText(t)
            if i >= 0:
                self.map_combo.setCurrentIndex(i)
        self._refresh_map_list()

    def load_shipped_defaults_if_present(self) -> None:
        p = default_presets_path()
        if not p.is_file():
            return
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
            presets = data.get("presets", [])
            if isinstance(presets, list) and presets:
                first = presets[0]
                self._apply_launch_dict(first.get("launch", first))
                adv = first.get("advanced")
                if isinstance(adv, list):
                    self.main_window.advanced_tab.set_settings(adv)
                self._update_preview()
        except (json.JSONDecodeError, OSError):
            pass
