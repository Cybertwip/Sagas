#!/usr/bin/env python3
"""Launch the Sagas animation sequence viewer (Python + Qt)."""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from sequence_viewer.__main__ import main

if __name__ == "__main__":
    raise SystemExit(main())
