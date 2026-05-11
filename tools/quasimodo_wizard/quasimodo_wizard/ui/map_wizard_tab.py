"""
Map Wizard tab — analyze / convert legacy map sources, then compile with bundled compilers/Q220 q2tool.
"""
from __future__ import annotations

from PySide6.QtWidgets import (
    QWidget,
    QHBoxLayout,
    QVBoxLayout,
    QListWidget,
    QStackedWidget,
    QLabel,
)
from ui.pages.select_page import SelectPage
from ui.pages.analyze_page import AnalyzePage
from ui.pages.entity_page import EntityPage
from ui.pages.compile_page import CompilePage


class MapWizardTab(QWidget):
    """Contains the original sidebar + stacked pages workflow."""

    def __init__(self, main_window):
        super().__init__()
        self.main_window = main_window
        outer = QVBoxLayout(self)
        outer.addWidget(
            QLabel(
                "Map Wizard — analyze / convert legacy map sources, then compile for Quasimodo RTX / Quake II using "
                "the bundled compilers/Q220 q2tool (BSP, VIS, RAD)."
            )
        )
        row = QHBoxLayout()
        self.sidebar = QListWidget()
        self.sidebar.addItems(["Select Map", "Analyze", "Entities", "Compile Map"])
        self.sidebar.setFixedWidth(160)
        self.sidebar.currentRowChanged.connect(self._switch_page)
        self.pages = QStackedWidget()
        self.select_page = SelectPage(main_window)
        self.analyze_page = AnalyzePage(main_window)
        self.entity_page = EntityPage(main_window)
        self.compile_page = CompilePage(main_window)
        self.pages.addWidget(self.select_page)
        self.pages.addWidget(self.analyze_page)
        self.pages.addWidget(self.entity_page)
        self.pages.addWidget(self.compile_page)
        row.addWidget(self.sidebar)
        row.addWidget(self.pages, stretch=1)
        outer.addLayout(row)
        self.sidebar.setCurrentRow(0)

    def _switch_page(self, index: int) -> None:
        self.pages.setCurrentIndex(index)
        if index == 1:
            self.analyze_page.update_page()
        elif index == 2:
            self.entity_page.update_page()
        elif index == 3:
            self.compile_page.update_page()
