# entity_page.py - Entity conversion page

from PySide6.QtWidgets import QWidget, QVBoxLayout, QPushButton, QTextEdit, QLabel, QFileDialog, QHBoxLayout
from PySide6.QtCore import Qt
import os
from core.entities import convert_entities

class EntityPage(QWidget):
    def __init__(self, main_window):
        super().__init__()
        self.main_window = main_window

        layout = QVBoxLayout(self)
        btn_layout = QHBoxLayout()
        self.convert_btn = QPushButton("Run Entity Conversion")
        self.convert_btn.clicked.connect(self.run_conversion)
        self.export_btn = QPushButton("Export Converted Map")
        self.export_btn.clicked.connect(self.export_map)
        self.export_btn.setEnabled(False)
        self.folder_btn = QPushButton("Select Output Folder")
        self.folder_btn.clicked.connect(self.select_output_folder)
        btn_layout.addWidget(self.convert_btn)
        btn_layout.addWidget(self.export_btn)
        btn_layout.addWidget(self.folder_btn)
        layout.addLayout(btn_layout)
        layout.addWidget(QLabel("Conversion Report:"))
        self.report_box = QTextEdit()
        self.report_box.setReadOnly(True)
        layout.addWidget(self.report_box)
        layout.addStretch()
        self.converted_text = None


    def run_conversion(self):
        text = self.main_window.state.get("map_text")
        if not text:
            self.report_box.setPlainText("No map file loaded.")
            self.converted_text = None
            self.export_btn.setEnabled(False)
            return
        converted, report = convert_entities(text)
        self.converted_text = converted
        self.report_box.setPlainText(report)
        self.export_btn.setEnabled(True)

    def update_page(self):
        self.report_box.setPlainText("")
        self.converted_text = None
        self.export_btn.setEnabled(False)

    def select_output_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Select Output Folder")
        if folder:
            self.main_window.state["output_folder"] = folder

    def export_map(self):
        if not self.converted_text:
            self.report_box.setPlainText("No converted map to export. Run conversion first.")
            return
        output_folder = self.main_window.state.get("output_folder")
        input_path = self.main_window.state.get("selected_path")
        if not output_folder:
            self.report_box.setPlainText("Please select an output folder first.")
            return
        if not input_path:
            self.report_box.setPlainText("No input map selected.")
            return
        base_name = os.path.splitext(os.path.basename(input_path))[0]
        out_path = os.path.join(output_folder, base_name + "_q2.map")
        try:
            with open(out_path, "w", encoding="utf-8") as f:
                f.write(self.converted_text)
            self.report_box.setPlainText(self.report_box.toPlainText() + f"\n\nExported to: {out_path}")
        except Exception as e:
            self.report_box.setPlainText(f"Failed to export: {e}")
