# analyze_page.py - Analyze map page
from PySide6.QtWidgets import QWidget, QVBoxLayout, QPushButton, QLabel, QTextEdit
from PySide6.QtCore import Qt
from core.analyzer import analyze_map

class AnalyzePage(QWidget):
    def __init__(self, main_window):
        super().__init__()
        self.main_window = main_window
        layout = QVBoxLayout(self)
        self.analyze_btn = QPushButton("Run Analysis")
        self.analyze_btn.clicked.connect(self.run_analysis)
        layout.addWidget(self.analyze_btn)
        self.result_labels = {}
        for key in ["brushes", "entities", "texture_count", "score"]:
            lbl = QLabel(f"{key.capitalize()}: -")
            self.result_labels[key] = lbl
            layout.addWidget(lbl)
        self.reason_label = QTextEdit()
        self.reason_label.setReadOnly(True)
        self.reason_label.setFixedHeight(60)
        layout.addWidget(QLabel("Reason:"))
        layout.addWidget(self.reason_label)
        layout.addStretch()

    def run_analysis(self):
        path = self.main_window.state.get("selected_path")
        if not path:
            self.reason_label.setPlainText("No file selected.")
            return
        result = analyze_map(path)
        self.main_window.state["analysis_result"] = result
        for key in ["brushes", "entities", "texture_count", "score"]:
            self.result_labels[key].setText(f"{key.capitalize()}: {result.get(key, '-')}")
        self.reason_label.setPlainText(result.get("reason", ""))

    def update_page(self):
        # Refresh display if analysis already run
        result = self.main_window.state.get("analysis_result")
        if result:
            for key in ["brushes", "entities", "texture_count", "score"]:
                self.result_labels[key].setText(f"{key.capitalize()}: {result.get(key, '-')}")
            self.reason_label.setPlainText(result.get("reason", ""))
        else:
            for key in self.result_labels:
                self.result_labels[key].setText(f"{key.capitalize()}: -")
            self.reason_label.setPlainText("")
