# main_window.py — Quasimodo Wizard main window (Launch / Map Wizard / Advanced).
from __future__ import annotations

import os

from PySide6.QtWidgets import QMainWindow, QTabWidget
from PySide6.QtGui import QIcon

from ui.launch_tab import LaunchTab
from ui.map_wizard_tab import MapWizardTab
from ui.advanced_tab import AdvancedTab


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

        tabs = QTabWidget()
        self.advanced_tab = AdvancedTab(self)
        self.launch_tab = LaunchTab(self)
        self.map_wizard_tab = MapWizardTab(self)
        tabs.addTab(self.launch_tab, "Launch")
        tabs.addTab(self.map_wizard_tab, "Map Wizard")
        tabs.addTab(self.advanced_tab, "Advanced")
        self.setCentralWidget(tabs)

        self.advanced_tab.rowsChanged.connect(self.launch_tab._update_preview)
