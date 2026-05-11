"""
Launch tab — run ``q2rtx.exe`` with common +set options and presets (no gameplay logic).

Engine cvar audit (read-only; names from ``src/refresh/vkpt/main.c``, ``src/client/refresh.c``,
``src/client/ui_rmlui.cpp``, ``baseq2/ui/settings/settings_data.h`` / ``q2rtx.menu``):

- ``vid_geometry`` — ``WxH`` (and optional ``+x+y``); primary resolution control (not ``vid_width`` / ``vid_height``).
- ``vid_fullscreen`` — ``0`` windowed, ``1`` fullscreen.
- ``vid_vsync`` — optional vsync (SDL / GL path); included as optional display extra.
- ``pt_dlss``, ``pt_dlss_quality`` — DLSS toggle and quality (0 quality … 4 DLAA per shipped UI docs).
- ``pt_restir_di`` — ReSTIR DI (0 legacy, 1 ReSTIR).
- ``flt_enable`` — master denoiser / SVGF reconstruction toggle.
- ``pt_nrd`` — ReSTIR denoiser choice (0 ASVGF, 1 NRD); engine gates NRD with ReSTIR (``main.c``).
- ``ui_rmlui``, ``ui_splash`` — RmlUi vs legacy UI; splash screen.

No dedicated ``vid_refreshrate`` / ``vid_displayfrequency`` cvar was found in this repo audit; refresh UI is
informational unless the user sets a matching cvar via Advanced.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

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

# --- Engine cvar names (do not rename without re-auditing engine) ---
CVAR_VID_GEOMETRY = "vid_geometry"
CVAR_VID_FULLSCREEN = "vid_fullscreen"
CVAR_VID_VSYNC = "vid_vsync"
CVAR_PT_DLSS = "pt_dlss"
CVAR_PT_DLSS_QUALITY = "pt_dlss_quality"
CVAR_PT_RESTIR_DI = "pt_restir_di"
CVAR_FLT_ENABLE = "flt_enable"
CVAR_PT_NRD = "pt_nrd"
CVAR_UI_RMLUI = "ui_rmlui"
CVAR_UI_SPLASH = "ui_splash"

ASPECT_RESOLUTIONS: Dict[str, List[str]] = {
    "16:9": ["1280x720", "1600x900", "1920x1080", "2560x1440", "3200x1800", "3840x2160"],
    "16:10": ["1280x800", "1440x900", "1680x1050", "1920x1200", "2560x1600", "3840x2400"],
    "21:9": ["2560x1080", "3440x1440", "3840x1600", "5120x2160"],
    "4:3": ["1024x768", "1280x960", "1600x1200", "2048x1536"],
    "5:4": ["1280x1024", "2560x2048"],
}

REFRESH_OPTIONS = ["Default", "60", "75", "90", "120", "144", "165", "240"]


def _safe_folder_open(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    if sys.platform == "win32":
        os.startfile(path)  # nosec: trusted user action opening repo-relative folder
    else:
        subprocess.Popen(["xdg-open", str(path)])  # nosec


def _parse_wh(res: str) -> Optional[Tuple[int, int]]:
    m = re.match(r"^(\d+)x(\d+)$", res.strip(), re.I)
    if not m:
        return None
    return int(m.group(1)), int(m.group(2))


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

        disp = QGroupBox("Display")
        dl = QFormLayout(disp)
        self.aspect_combo = QComboBox()
        self.aspect_combo.addItems(list(ASPECT_RESOLUTIONS.keys()) + ["Custom"])
        self.aspect_combo.setCurrentText("16:9")
        self.aspect_combo.currentTextChanged.connect(self._on_aspect_changed)
        self.res_combo = QComboBox()
        self.res_combo.currentTextChanged.connect(self._on_resolution_combo_changed)
        self.w_spin = QSpinBox()
        self.w_spin.setRange(320, 8192)
        self.h_spin = QSpinBox()
        self.h_spin.setRange(240, 8192)
        self.w_spin.valueChanged.connect(self._on_manual_res_changed)
        self.h_spin.valueChanged.connect(self._on_manual_res_changed)
        self.fullscreen = QComboBox()
        self.fullscreen.addItems(["Windowed (vid_fullscreen 0)", "Fullscreen (1)"])
        self.fullscreen.currentIndexChanged.connect(self._update_preview)
        self.refresh_combo = QComboBox()
        self.refresh_combo.addItems(REFRESH_OPTIONS)
        self.refresh_combo.setToolTip(
            "No dedicated refresh-rate cvar was found in this repository audit. "
            "Selection is for your notes only unless you add a matching +set row in Advanced."
        )
        self.refresh_combo.currentTextChanged.connect(self._update_preview)
        self.vsync_combo = QComboBox()
        self.vsync_combo.addItems(["Default (omit vid_vsync)", "vid_vsync 0", "vid_vsync 1"])
        self.vsync_combo.currentIndexChanged.connect(self._update_preview)
        dl.addRow("Aspect ratio:", self.aspect_combo)
        dl.addRow("Resolution:", self.res_combo)
        dl.addRow("Width (Custom):", self.w_spin)
        dl.addRow("Height (Custom):", self.h_spin)
        dl.addRow("Display mode:", self.fullscreen)
        dl.addRow("Refresh rate (informational):", self.refresh_combo)
        dl.addRow("VSync:", self.vsync_combo)
        root.addWidget(disp)

        dlss = QGroupBox("Upscaling / DLSS")
        dssl = QFormLayout(dlss)
        self.pt_dlss = QComboBox()
        self.pt_dlss.addItems(["Default (omit pt_dlss)", "Off (+set pt_dlss 0)", "On (+set pt_dlss 1)"])
        self.pt_dlss.currentIndexChanged.connect(self._on_dlss_changed)
        self.pt_dlss_quality = QComboBox()
        self.pt_dlss_quality.addItems(
            [
                "Default (omit pt_dlss_quality)",
                "Quality (0)",
                "Balanced (1)",
                "Performance (2)",
                "Ultra Performance (3)",
                "DLAA (4)",
            ]
        )
        self.pt_dlss_quality.setToolTip("pt_dlss_quality — only sent when DLSS is On.")
        self.pt_dlss_quality.currentIndexChanged.connect(self._update_preview)
        dssl.addRow("DLSS (pt_dlss):", self.pt_dlss)
        dssl.addRow("DLSS mode (pt_dlss_quality):", self.pt_dlss_quality)
        root.addWidget(dlss)

        restir_g = QGroupBox("Lighting / ReSTIR")
        rl = QFormLayout(restir_g)
        self.pt_restir_di = QComboBox()
        self.pt_restir_di.addItems(["Default (omit pt_restir_di)", "Off — legacy (+set pt_restir_di 0)", "On — ReSTIR (+set pt_restir_di 1)"])
        self.pt_restir_di.currentIndexChanged.connect(self._on_restir_changed)
        rl.addRow("ReSTIR DI (pt_restir_di):", self.pt_restir_di)
        root.addWidget(restir_g)

        den_g = QGroupBox("Denoiser")
        denl = QFormLayout(den_g)
        self.denoiser = QComboBox()
        self.denoiser.addItems(
            [
                "Default (omit flt_enable / pt_nrd)",
                "Off (+set flt_enable 0)",
                "ASVGF — ReSTIR path (+set flt_enable 1; +set pt_nrd 0)",
                "NRD — ReSTIR path (+set flt_enable 1; +set pt_nrd 1)",
            ]
        )
        self.denoiser.setToolTip(
            "Engine uses pt_nrd only with ReSTIR DI enabled. NRD is disabled in this profile when ReSTIR DI is Off."
        )
        self.denoiser.currentIndexChanged.connect(self._update_preview)
        denl.addRow("Profile:", self.denoiser)
        root.addWidget(den_g)

        ui_g = QGroupBox("UI system")
        uil = QFormLayout(ui_g)
        self.ui_rmlui = QComboBox()
        self.ui_rmlui.addItems(["Default (omit ui_rmlui)", "RmlUi modern (+set ui_rmlui 1)", "Legacy (+set ui_rmlui 0)"])
        self.ui_rmlui.currentIndexChanged.connect(self._update_preview)
        self.ui_splash = QCheckBox("Startup splash (+set ui_splash 1)")
        self.ui_splash.setChecked(False)
        self.ui_splash.stateChanged.connect(lambda *_: self._update_preview())
        uil.addRow("UI:", self.ui_rmlui)
        uil.addRow(self.ui_splash)
        root.addWidget(ui_g)

        ter = QGroupBox("Terrain / diagnostics")
        tl = QFormLayout(ter)
        self.terrain_enable = QCheckBox("terrain_enable 1")
        self.terrain_enable.setChecked(True)
        self.terrain_collision = QCheckBox("terrain_collision 1")
        self.terrain_collision.setChecked(True)
        self.terrain_water = QCheckBox("terrain_water 1")
        self.terrain_water.setChecked(True)
        self.terrain_rtx = QCheckBox("terrain_rtx_instance 1")
        self.terrain_rtx.setChecked(True)
        self.jolt_compare = QCheckBox(
            "terrain_collision_backend 1 — Jolt compare diagnostics only; gameplay remains legacy collision."
        )
        self.jolt_compare.setChecked(False)
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
            self.logfile,
            self.developer,
        ):
            w.stateChanged.connect(lambda *_: self._update_preview())
        tl.addRow(self.terrain_enable)
        tl.addRow(self.terrain_collision)
        tl.addRow(self.terrain_water)
        tl.addRow(self.terrain_rtx)
        tl.addRow(self.jolt_compare)
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
        pr = QHBoxLayout()
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

        root.addWidget(QLabel("Command preview (repo root is working directory). Core +sets first; Advanced rows only fill gaps:"))
        self.preview = QPlainTextEdit()
        self.preview.setReadOnly(True)
        self.preview.setMaximumBlockCount(200)
        self.preview.setMinimumHeight(120)
        root.addWidget(self.preview)

        self._load_defaults()
        self._load_app_settings()
        if not app_settings_path().is_file():
            self.load_shipped_defaults_if_present()
        self._refresh_map_list()
        self._refresh_preset_list()
        self._populate_res_combo()
        self._sync_resolution_widgets()
        self._on_dlss_changed()
        self._on_restir_changed()
        self._update_preview()

    # --- Display wiring ---
    def _on_aspect_changed(self, _t: str = "") -> None:
        self._populate_res_combo()
        self._sync_resolution_widgets()
        self._update_preview()

    def _populate_res_combo(self) -> None:
        asp = self.aspect_combo.currentText()
        self.res_combo.blockSignals(True)
        self.res_combo.clear()
        if asp == "Custom":
            self.res_combo.addItem("(use width/height fields)")
            self.res_combo.setEnabled(False)
        else:
            self.res_combo.setEnabled(True)
            for r in ASPECT_RESOLUTIONS.get(asp, []):
                self.res_combo.addItem(r)
        self.res_combo.blockSignals(False)

    def _sync_resolution_widgets(self) -> None:
        custom = self.aspect_combo.currentText() == "Custom"
        self.w_spin.setEnabled(custom)
        self.h_spin.setEnabled(custom)
        if not custom and self.res_combo.count() > 0:
            wh = _parse_wh(self.res_combo.currentText())
            if wh:
                self.w_spin.blockSignals(True)
                self.h_spin.blockSignals(True)
                self.w_spin.setValue(wh[0])
                self.h_spin.setValue(wh[1])
                self.w_spin.blockSignals(False)
                self.h_spin.blockSignals(False)

    def _on_resolution_combo_changed(self, _t: str = "") -> None:
        self._sync_resolution_widgets()
        self._update_preview()

    def _on_manual_res_changed(self) -> None:
        if self.aspect_combo.currentText() == "Custom":
            self._update_preview()

    def _on_dlss_changed(self) -> None:
        on = self.pt_dlss.currentIndex() == 2
        self.pt_dlss_quality.setEnabled(on)
        if not on:
            self.pt_dlss_quality.setCurrentIndex(0)
        self._update_preview()

    def _on_restir_changed(self) -> None:
        restir_on = self.pt_restir_di.currentIndex() == 2
        # ASVGF/NRD ReSTIR profiles require ReSTIR DI (matches engine gating for pt_nrd).
        if not restir_on and self.denoiser.currentIndex() in (2, 3):
            self.denoiser.blockSignals(True)
            self.denoiser.setCurrentIndex(0)
            self.denoiser.blockSignals(False)
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

    def _selected_map(self) -> str:
        manual = self.map_manual.text().strip()
        if manual:
            return manual
        t = self.map_combo.currentText().strip()
        if t and t != "(none)":
            return t
        return ""

    def _geometry_string(self) -> str:
        return f"{self.w_spin.value()}x{self.h_spin.value()}"

    def _core_cvar_pairs(self) -> List[Tuple[str, str]]:
        """Ordered (name, value) pairs from Launch tab controls only."""
        out: List[Tuple[str, str]] = []
        out.append((CVAR_VID_GEOMETRY, self._geometry_string()))
        out.append((CVAR_VID_FULLSCREEN, str(self.fullscreen.currentIndex())))

        vsi = self.vsync_combo.currentIndex()
        if vsi == 1:
            out.append((CVAR_VID_VSYNC, "0"))
        elif vsi == 2:
            out.append((CVAR_VID_VSYNC, "1"))

        di = self.pt_dlss.currentIndex()
        if di == 1:
            out.append((CVAR_PT_DLSS, "0"))
        elif di == 2:
            out.append((CVAR_PT_DLSS, "1"))
            qi = self.pt_dlss_quality.currentIndex()
            if qi > 0:
                out.append((CVAR_PT_DLSS_QUALITY, str(qi - 1)))

        ri = self.pt_restir_di.currentIndex()
        if ri == 1:
            out.append((CVAR_PT_RESTIR_DI, "0"))
        elif ri == 2:
            out.append((CVAR_PT_RESTIR_DI, "1"))

        den = self.denoiser.currentIndex()
        restir_on = self.pt_restir_di.currentIndex() == 2
        if den == 1:
            out.append((CVAR_FLT_ENABLE, "0"))
        elif den == 2 and restir_on:
            out.append((CVAR_FLT_ENABLE, "1"))
            out.append((CVAR_PT_NRD, "0"))
        elif den == 3 and restir_on:
            out.append((CVAR_FLT_ENABLE, "1"))
            out.append((CVAR_PT_NRD, "1"))

        ui = self.ui_rmlui.currentIndex()
        if ui == 1:
            out.append((CVAR_UI_RMLUI, "1"))
        elif ui == 2:
            out.append((CVAR_UI_RMLUI, "0"))
        out.append((CVAR_UI_SPLASH, "1" if self.ui_splash.isChecked() else "0"))

        out.append(("terrain_enable", "1" if self.terrain_enable.isChecked() else "0"))
        out.append(("terrain_collision", "1" if self.terrain_collision.isChecked() else "0"))
        out.append(("terrain_water", "1" if self.terrain_water.isChecked() else "0"))
        out.append(("terrain_rtx_instance", "1" if self.terrain_rtx.isChecked() else "0"))
        out.append(("terrain_collision_backend", "1" if self.jolt_compare.isChecked() else "0"))
        if self.logfile.isChecked():
            out.append(("logfile", "2"))
            out.append(("logfile_flush", "1"))
        out.append(("developer", "1" if self.developer.isChecked() else "0"))
        out.append(("dedicated", "1" if self.dedicated.isChecked() else "0"))
        return out

    def build_argv(self) -> List[str]:
        exe = self._resolve_exe()
        if not exe:
            return []
        args: List[str] = [str(exe)]

        core_pairs = self._core_cvar_pairs()
        seen = {n for n, _ in core_pairs}
        for n, v in core_pairs:
            args.extend(["+set", n, v])

        for n, v in self.main_window.advanced_tab.get_cvar_pairs():
            if n and n not in seen:
                args.extend(["+set", n, v])
                seen.add(n)

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
        line = " ".join(parts)
        if self.refresh_combo.currentText() not in ("", "Default"):
            line += (
                "\n; Note: refresh preset is not emitted (no audited refresh-rate cvar). "
                "Add one via Advanced if needed."
            )
        return line

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
            "version": 2,
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
            "aspect_ratio": self.aspect_combo.currentText(),
            "resolution_preset": self.res_combo.currentText(),
            "width": self.w_spin.value(),
            "height": self.h_spin.value(),
            "fullscreen_index": self.fullscreen.currentIndex(),
            "refresh_preset": self.refresh_combo.currentText(),
            "vsync_index": self.vsync_combo.currentIndex(),
            "pt_dlss_index": self.pt_dlss.currentIndex(),
            "pt_dlss_quality_index": self.pt_dlss_quality.currentIndex(),
            "pt_restir_di_index": self.pt_restir_di.currentIndex(),
            "denoiser_index": self.denoiser.currentIndex(),
            "ui_rmlui_index": self.ui_rmlui.currentIndex(),
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

        if "aspect_ratio" in d:
            t = str(d["aspect_ratio"])
            i = self.aspect_combo.findText(t)
            if i >= 0:
                self.aspect_combo.setCurrentIndex(i)
        self._populate_res_combo()

        if "resolution_preset" in d:
            t = str(d["resolution_preset"])
            i = self.res_combo.findText(t)
            if i >= 0:
                self.res_combo.setCurrentIndex(i)

        if "width" in d:
            self.w_spin.setValue(int(d["width"]))
        if "height" in d:
            self.h_spin.setValue(int(d["height"]))

        if "fullscreen_index" in d:
            self.fullscreen.setCurrentIndex(int(d["fullscreen_index"]))

        if "refresh_preset" in d:
            t = str(d["refresh_preset"])
            i = self.refresh_combo.findText(t)
            if i >= 0:
                self.refresh_combo.setCurrentIndex(i)

        if "vsync_index" in d:
            self.vsync_combo.setCurrentIndex(int(d.get("vsync_index", 0)))

        if "pt_dlss_index" in d:
            self.pt_dlss.setCurrentIndex(int(d["pt_dlss_index"]))
        elif "pt_dlss_index" not in d and "resolution_preset" in d:
            # v1 presets used pt_dlss_index only
            pass
        if "pt_dlss_quality_index" in d:
            self.pt_dlss_quality.setCurrentIndex(int(d["pt_dlss_quality_index"]))

        if "pt_restir_di_index" in d:
            self.pt_restir_di.setCurrentIndex(int(d["pt_restir_di_index"]))

        if "denoiser_index" in d:
            self.denoiser.setCurrentIndex(int(d["denoiser_index"]))

        if "ui_rmlui_index" in d:
            self.ui_rmlui.setCurrentIndex(int(d["ui_rmlui_index"]))

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

        # v1 preset compatibility
        if "resolution_preset" in d and "aspect_ratio" not in d:
            rp = str(d["resolution_preset"])
            if re.match(r"^\d+x\d+$", rp):
                for asp, lst in ASPECT_RESOLUTIONS.items():
                    if rp in lst:
                        self.aspect_combo.setCurrentText(asp)
                        self._populate_res_combo()
                        idx = self.res_combo.findText(rp)
                        if idx >= 0:
                            self.res_combo.setCurrentIndex(idx)
                        break
                else:
                    self.aspect_combo.setCurrentText("Custom")
                    self._populate_res_combo()
                    wh = _parse_wh(rp)
                    if wh:
                        self.w_spin.setValue(wh[0])
                        self.h_spin.setValue(wh[1])

        if "pt_dlss_index" in d and "pt_dlss_quality_index" not in d:
            self.pt_dlss_quality.setCurrentIndex(0)

        self._sync_resolution_widgets()
        self._on_dlss_changed()
        self._on_restir_changed()
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
