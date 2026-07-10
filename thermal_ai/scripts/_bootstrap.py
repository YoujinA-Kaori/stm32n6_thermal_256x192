#!/usr/bin/env python3
"""Bootstrap helpers for thermal_ai scripts."""

from __future__ import annotations

import sys
from pathlib import Path


def setup_python_path() -> Path:
    """Add the thermal_ai project root to sys.path and return it."""
    thermal_ai_root = Path(__file__).resolve().parents[1]
    root_str = str(thermal_ai_root)
    if root_str not in sys.path:
        sys.path.insert(0, root_str)
    return thermal_ai_root
