# select_page.py - Select .map file page
from PySide6.QtWidgets import QWidget, QVBoxLayout, QPushButton, QLabel, QFileDialog
from PySide6.QtCore import Qt
import os

class SelectPage(QWidget):
    def __init__(self, main_window):
        super().__init__()
        self.main_window = main_window
        layout = QVBoxLayout(self)
        self.select_btn = QPushButton("Select .map file")
        self.select_btn.clicked.connect(self.select_file)
        self.path_label = QLabel("No file selected.")
        self.path_label.setWordWrap(True)
        layout.addWidget(self.select_btn)
        layout.addWidget(self.path_label)
        layout.addStretch()

    def select_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Quake 1 .map file (Map Wizard import)", "", "Quake 1 Map (*.map)"
        )
        if path:
            self.main_window.state["selected_path"] = path
            self.path_label.setText(f"Selected: {os.path.basename(path)}\n{path}")
            # Load file text for entity conversion
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                self.main_window.state["map_text"] = f.read()
        else:
            self.path_label.setText("No file selected.")
