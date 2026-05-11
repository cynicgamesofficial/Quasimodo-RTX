"""
Advanced tab — arbitrary +set cvar rows (expert). Included in Launch command preview and presets.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, List

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QCheckBox,
    QLineEdit,
    QLabel,
    QFileDialog,
    QMessageBox,
    QScrollArea,
)

from repo_paths import user_data_dir


class AdvancedTab(QWidget):
    rowsChanged = Signal()

    def __init__(self, main_window):
        super().__init__()
        self.main_window = main_window
        self._rows: List[dict[str, Any]] = []
        root = QVBoxLayout(self)
        root.addWidget(
            QLabel(
                "Add arbitrary engine cvars as +set name value. "
                "Only checked rows are appended to the Launch command. "
                "Invalid names are not validated — use with care."
            )
        )
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        self._rows_host = QWidget()
        self._rows_layout = QVBoxLayout(self._rows_host)
        self._rows_layout.addStretch()
        scroll.setWidget(self._rows_host)
        root.addWidget(scroll)

        btn_row = QHBoxLayout()
        self._btn_add = QPushButton("Add cvar row")
        self._btn_add.clicked.connect(lambda: self._add_row(True, "", ""))
        self._btn_load = QPushButton("Load advanced preset…")
        self._btn_load.clicked.connect(self._load_preset_file)
        self._btn_save = QPushButton("Save advanced preset…")
        self._btn_save.clicked.connect(self._save_preset_file)
        self._btn_reset = QPushButton("Reset")
        self._btn_reset.clicked.connect(self._reset)
        btn_row.addWidget(self._btn_add)
        btn_row.addWidget(self._btn_load)
        btn_row.addWidget(self._btn_save)
        btn_row.addWidget(self._btn_reset)
        btn_row.addStretch()
        root.addLayout(btn_row)

        self._add_row(False, "", "")

    def _emit(self) -> None:
        self.rowsChanged.emit()

    def _add_row(self, enabled: bool, name: str, value: str) -> None:
        roww = QWidget()
        lay = QHBoxLayout(roww)
        cb = QCheckBox()
        cb.setChecked(enabled)
        ne = QLineEdit()
        ne.setPlaceholderText("cvar")
        ne.setText(name)
        ve = QLineEdit()
        ve.setPlaceholderText("value")
        ve.setText(value)
        rm = QPushButton("Remove")
        lay.addWidget(cb)
        lay.addWidget(ne, stretch=1)
        lay.addWidget(ve, stretch=1)
        lay.addWidget(rm)
        self._rows_layout.insertWidget(self._rows_layout.count() - 1, roww)

        def remove():
            roww.setParent(None)
            roww.deleteLater()
            self._rows[:] = [r for r in self._rows if r["widget"] is not roww]
            self._emit()

        rm.clicked.connect(remove)

        def on_edit():
            self._emit()

        cb.stateChanged.connect(lambda *_: self._emit())
        ne.textChanged.connect(lambda *_: self._emit())
        ve.textChanged.connect(lambda *_: self._emit())

        self._rows.append({"widget": roww, "enabled": cb, "name": ne, "value": ve})
        self._emit()

    def _reset(self) -> None:
        for r in list(self._rows):
            r["widget"].setParent(None)
            r["widget"].deleteLater()
        self._rows.clear()
        self._add_row(False, "", "")
        self._emit()

    def get_cvar_pairs(self) -> List[tuple[str, str]]:
        out: List[tuple[str, str]] = []
        for r in self._rows:
            if not r["enabled"].isChecked():
                continue
            n = r["name"].text().strip()
            v = r["value"].text().strip()
            if n:
                out.append((n, v))
        return out

    def get_settings(self) -> List[dict[str, Any]]:
        rows = []
        for r in self._rows:
            rows.append(
                {
                    "enabled": r["enabled"].isChecked(),
                    "name": r["name"].text().strip(),
                    "value": r["value"].text(),
                }
            )
        return rows

    def set_settings(self, rows: List[dict[str, Any]]) -> None:
        for r in list(self._rows):
            r["widget"].setParent(None)
            r["widget"].deleteLater()
        self._rows.clear()
        if not rows:
            self._add_row(False, "", "")
            return
        for item in rows:
            self._add_row(
                bool(item.get("enabled", True)),
                str(item.get("name", "")),
                str(item.get("value", "")),
            )
        self._emit()

    def _load_preset_file(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Load advanced preset JSON", str(user_data_dir()), "JSON (*.json)"
        )
        if not path:
            return
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
            rows = data.get("rows", data) if isinstance(data, dict) else data
            if isinstance(rows, list):
                self.set_settings(rows)
        except Exception as e:
            QMessageBox.warning(self, "Load failed", str(e))

    def _save_preset_file(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, "Save advanced preset JSON", str(user_data_dir() / "advanced_preset.json"), "JSON (*.json)"
        )
        if not path:
            return
        try:
            Path(path).write_text(
                json.dumps({"rows": self.get_settings()}, indent=2), encoding="utf-8"
            )
        except Exception as e:
            QMessageBox.warning(self, "Save failed", str(e))
