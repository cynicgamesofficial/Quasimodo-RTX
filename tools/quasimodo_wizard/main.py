#!/usr/bin/env python3
"""
Quasimodo Wizard — entry point (repo-safe).

Run from repository: ``python tools/quasimodo_wizard/main.py``
or ``tools\\quasimodo_wizard\\run_quasimodo_wizard.bat`` from any cwd (script cds to its folder).

Packaging (manual, does not run in CI by default)::
    pyinstaller --onefile --windowed --name launcher ^
      --icon tools/quasimodo_wizard/assets/icons/quasimodo_wizard.ico ^
      tools/quasimodo_wizard/main.py
See ``tools/quasimodo_wizard/package_launcher.ps1``.
"""
from __future__ import annotations

import sys
from pathlib import Path

_TOOL_ROOT = Path(__file__).resolve().parent
_PKG = _TOOL_ROOT / "quasimodo_wizard"
if not _PKG.is_dir():
    print("Quasimodo Wizard: missing package directory:", _PKG, file=sys.stderr)
    sys.exit(1)
sys.path.insert(0, str(_PKG))

from PySide6.QtWidgets import QApplication  # noqa: E402

from ui.main_window import MainWindow  # noqa: E402


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("Quasimodo Wizard")
    app.setOrganizationName("QuasimodoRTX")
    win = MainWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
