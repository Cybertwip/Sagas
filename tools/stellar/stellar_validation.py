"""Project validation independent from Stellar's CLI and desktop UI."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable


def validate_project(project: Any, project_path: Path,
                     resolve_path: Callable[[str, Path], Path],
                     load_joint_tree: Callable[[str], list[dict[str, Any]]],
                     load_setup_mask: Callable[[str], int]) -> list[tuple[str, str]]:
    """Return user-facing errors and warnings for one editable project."""
    issues: list[tuple[str, str]] = []
    for label, value in (("model", project.model_path), ("portrait", project.portrait_path)):
        if not value:
            issues.append(("error", f"No {label} is assigned."))
        elif not resolve_path(value, project_path.parent).exists():
            issues.append(("error", f"{label.title()} does not exist: {value}"))
    if not project.source_manifest.get("bones"):
        issues.append(("warning", "FBX has not been inspected; source skeleton is unknown."))

    required_ids = {5, 6, 10, 12, 16, 19, 20, 22, 24, 25, 27}
    required = [mapping for mapping in project.bone_mappings if mapping.game_id in required_ids]
    missing = [mapping.target for mapping in required if mapping.enabled and not mapping.source]
    if missing:
        level = "error" if project.strict_skeleton else "warning"
        issues.append((level, "Required game joints are unmapped: " + ", ".join(missing)))

    base_tree = load_joint_tree(project.base_character)
    setup_mask = load_setup_mask(project.base_character)
    if not base_tree:
        issues.append(("error", f"Could not read {project.base_character}'s base joint topology."))
    else:
        created = {3}
        for index, entry in enumerate(base_tree):
            if index >= 32 or not (setup_mask & (1 << (31 - index))):
                continue
            parent = int(entry["parent"])
            if parent not in created:
                issues.append((
                    "error",
                    f"{project.base_character} joint {entry['game_id']} has omitted parent {parent}.",
                ))
            created.add(int(entry["game_id"]))

    duplicates: dict[str, list[str]] = {}
    for mapping in project.bone_mappings:
        if mapping.source:
            duplicates.setdefault(mapping.source, []).append(mapping.target)
    duplicate_names = {name: targets for name, targets in duplicates.items() if len(targets) > 1}
    if duplicate_names:
        issues.append(("warning", "One source bone drives multiple game joints: " +
                       "; ".join(f"{name} -> {', '.join(targets)}"
                                 for name, targets in duplicate_names.items())))

    for hit in project.hitboxes:
        if hit.start_frame > hit.end_frame:
            issues.append(("error", f"{hit.move} hitbox {hit.attack_id}: start is after end."))
        if not 0 <= hit.attack_id <= 7:
            issues.append(("error", f"{hit.move}: attack ID {hit.attack_id} is outside 0..7."))
        if not -64 <= hit.joint_id <= 63:
            issues.append(("error", f"{hit.move}: joint ID {hit.joint_id} cannot fit the motion command."))
        if not 0 <= hit.damage <= 255 or not 0 <= hit.size <= 65535:
            issues.append(("error", f"{hit.move}: damage/size exceeds the command bit field."))
        if not -512 <= hit.angle <= 511:
            issues.append(("error", f"{hit.move}: angle {hit.angle} exceeds signed 10-bit range."))
        if not (0 <= hit.knockback_scale <= 1023 and
                0 <= hit.knockback_weight <= 1023 and
                0 <= hit.knockback_base <= 1023):
            issues.append(("error", f"{hit.move}: a knockback value exceeds 10-bit range."))
    for sound in project.sounds:
        path = resolve_path(sound.path, project_path.parent) if sound.path else None
        if not path or not path.exists():
            issues.append(("warning", f"Sound '{sound.name}' has no readable source."))
    if not issues:
        issues.append(("ok", "Project is internally consistent."))
    return issues
