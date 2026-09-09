"""Batch project discovery and fault-tolerant export orchestration."""

from __future__ import annotations

from pathlib import Path
import sys
from typing import Any, Callable, Iterable


def discover_projects(inputs: Iterable[Path]) -> list[Path]:
    """Find editable project roots, excluding generated export artifacts."""
    projects: dict[str, Path] = {}
    excluded_dirs = {"stellarexport", "game_overlay", ".stellar", "__pycache__"}
    for raw_input in inputs:
        source = raw_input.expanduser().resolve()
        if not source.exists():
            raise FileNotFoundError(f"Batch export input not found: {source}")
        if source.is_file():
            candidates = [source]
        elif source.is_dir():
            candidates = []
            direct_project = source / "stellar_project.json"
            if direct_project.is_file():
                candidates.append(direct_project)
            for candidate in source.rglob("stellar_project.json"):
                relative_dirs = candidate.relative_to(source).parts[:-1]
                if any(part.casefold() in excluded_dirs or part.startswith(".")
                       for part in relative_dirs):
                    continue
                candidates.append(candidate)
        else:
            raise ValueError(f"Batch export input is not a file or directory: {source}")
        for candidate in candidates:
            resolved = candidate.resolve()
            projects[str(resolved).casefold()] = resolved
    return sorted(projects.values(), key=lambda path: str(path).casefold())


def export_projects(inputs: Iterable[Path], load_project: Callable[[Path], Any],
                    export_project: Callable[[Any, Path, Path, Callable[[str], None]], Any]) -> int:
    """Export every discovered project and report all failures at the end."""
    project_paths = discover_projects(inputs)
    if not project_paths:
        raise ValueError("Batch export found no stellar_project.json files")
    failures: list[tuple[Path, str]] = []
    total = len(project_paths)
    print(f"Stellar batch export: {total} project(s)")
    for index, project_path in enumerate(project_paths, start=1):
        label = f"[{index}/{total}] {project_path.parent.name}"
        print(f"{label}: loading {project_path}")
        try:
            project = load_project(project_path)
            export_project(project, project_path, project_path.parent / "StellarExport",
                           lambda message, prefix=label: print(f"{prefix}: {message}"))
        except Exception as exc:
            failures.append((project_path, str(exc)))
            print(f"{label}: FAILED: {exc}", file=sys.stderr)
    succeeded = total - len(failures)
    print(f"Stellar batch export complete: {succeeded} succeeded, {len(failures)} failed")
    if failures:
        for project_path, message in failures:
            print(f"FAILED {project_path}: {message}", file=sys.stderr)
        return 1
    return 0
