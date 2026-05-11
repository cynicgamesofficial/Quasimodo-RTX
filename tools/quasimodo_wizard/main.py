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
import traceback
from pathlib import Path

# PyInstaller one-file: script lives next to bundled ``quasimodo_wizard/``, ``presets/``, ``assets/`` under sys._MEIPASS.
_TOOL_ROOT = Path(__file__).resolve().parent
_PKG = _TOOL_ROOT / "quasimodo_wizard"
if not _PKG.is_dir():
    print("Quasimodo Wizard: missing package directory:", _PKG, file=sys.stderr)
    sys.exit(1)
sys.path.insert(0, str(_PKG.resolve()))


def _install_frozen_crash_log() -> None:
    """If the GUI build crashes with no console, write the last traceback to %TEMP%."""
    if not getattr(sys, "frozen", False):
        return

    def _hook(exc_type, exc, tb):  # noqa: ANN001
        try:
            import tempfile

            p = Path(tempfile.gettempdir()) / "quasimodo_wizard_last_error.txt"
            text = "".join(traceback.format_exception(exc_type, exc, tb))
            p.write_text(text, encoding="utf-8", errors="replace")
        except OSError:
            pass
        sys.__excepthook__(exc_type, exc, tb)

    sys.excepthook = _hook


_install_frozen_crash_log()

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
