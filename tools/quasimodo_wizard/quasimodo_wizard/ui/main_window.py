# main_window.py — Quasimodo Wizard main window (Launch / Advanced / Map Wizard).
from __future__ import annotations

import os

from PySide6.QtCore import QSize
from PySide6.QtGui import QIcon
from PySide6.QtWidgets import (
    QMainWindow,
    QTabWidget,
    QWidget,
    QVBoxLayout,
    QLabel,
    QSizePolicy,
)

from repo_paths import wizard_asset_path

from ui.launch_tab import LaunchTab
from ui.map_wizard_tab import MapWizardTab
from ui.advanced_tab import AdvancedTab

try:
    from PySide6.QtSvgWidgets import QSvgWidget
except ImportError:  # pragma: no cover
    QSvgWidget = None  # type: ignore[misc,assignment]


class _AspectQSvgHeader(QWidget):
    """
    ``QSvgWidget`` scales SVG to fill its rect; distortion happens when the rect’s aspect
    does not match the document. We size the widget to ``width × (width * vh/vw)`` so the
    rect matches the SVG viewBox aspect ratio.

    Uses only ``PySide6.QtSvgWidgets`` (PyInstaller bundles this without a separate
    ``PySide6.QtSvg`` import path on some builds).
    """

    def __init__(self, svg_path: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        if QSvgWidget is None:
            raise ImportError("PySide6.QtSvgWidgets.QSvgWidget is not available")
        self._svg = QSvgWidget(svg_path, self)
        sh = self._svg.sizeHint()
        self._vw = max(1, sh.width())
        self._vh = max(1, sh.height())
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.setMinimumWidth(80)

    def sizeHint(self) -> QSize:
        w = 640
        return QSize(w, max(24, int(w * self._vh / self._vw)))

    def minimumSizeHint(self) -> QSize:
        w = 200
        return QSize(w, max(24, int(w * self._vh / self._vw)))

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        w = max(1, self.width())
        h = max(1, int(w * self._vh / self._vw))
        if abs(self.height() - h) > 1:
            self.setFixedHeight(h)
        self._svg.setGeometry(0, 0, w, h)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Quasimodo Wizard")
        icon_path = os.path.join(os.path.dirname(__file__), "..", "resources", "icon.svg")
        self.setWindowIcon(QIcon(icon_path))
        self.resize(920, 620)

        self.state = {
            "selected_path": None,
            "analysis_result": None,
            "map_text": None,
            "output_folder": None,
        }

        header_wrap = QWidget()
        header_layout = QVBoxLayout(header_wrap)
        header_layout.setContentsMargins(4, 4, 4, 0)
        svg_path = wizard_asset_path("branding", "rtx_launcher_header.svg")
        header_ok = False
        try:
            if svg_path.is_file():
                svg_widget = _AspectQSvgHeader(str(svg_path))
                header_layout.addWidget(svg_widget)
                header_ok = True
        except (ImportError, OSError, RuntimeError):
            pass
        if not header_ok:
            title = QLabel("Quasimodo Wizard")
            title.setStyleSheet("font-size: 18px; font-weight: bold; padding: 8px;")
            header_layout.addWidget(title)

        tabs = QTabWidget()
        self.advanced_tab = AdvancedTab(self)
        self.launch_tab = LaunchTab(self)
        self.map_wizard_tab = MapWizardTab(self)
        tabs.addTab(self.launch_tab, "Launch")
        tabs.addTab(self.advanced_tab, "Advanced")
        tabs.addTab(self.map_wizard_tab, "Map Wizard")

        central = QWidget()
        cl = QVBoxLayout(central)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.addWidget(header_wrap)
        cl.addWidget(tabs)
        self.setCentralWidget(central)

        self.advanced_tab.rowsChanged.connect(self.launch_tab._update_preview)
