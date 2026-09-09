"""Dependency-light helpers shared by Stellar's CLI, export, and GUI layers."""

from __future__ import annotations

import json
import math
import os
from pathlib import Path
import re
import tempfile
import time
from typing import Any


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S%z")


def clean_symbol(name: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "", name.title().replace(" ", ""))
    if not result or result[0].isdigit():
        result = "Fighter" + result
    return result


def fbx_texture_size(width: int, height: int, maximum: int = 512) -> tuple[int, int]:
    """Return an aspect-preserving texture size capped on its longest side."""
    width = max(1, int(width))
    height = max(1, int(height))
    maximum = max(1, int(maximum))
    longest = max(width, height)
    if longest <= maximum:
        return width, height
    scale = maximum / longest
    return (max(1, int(math.floor(width * scale + 0.5))),
            max(1, int(math.floor(height * scale + 0.5))))


def normalize_bone(name: str) -> str:
    text = name.casefold().replace("mixamorig", "")
    return re.sub(r"[^a-z0-9]", "", text)


def relpath_or_abs(path: Path, base: Path) -> str:
    try:
        return str(path.resolve().relative_to(base.resolve()))
    except ValueError:
        return str(path.resolve())


def resolve_project_path(value: str, project_dir: Path) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else (project_dir / path).resolve()


def atomic_json(path: Path, data: Any) -> None:
    """Atomically persist editable project data without partial JSON files."""
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            json.dump(data, stream, indent=2, ensure_ascii=False)
            stream.write("\n")
        os.replace(temp_name, path)
    except BaseException:
        try:
            os.unlink(temp_name)
        except OSError:
            pass
        raise
