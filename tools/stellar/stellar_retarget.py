# Ported authoring/Blender services from remix/game/custom/Toolset/stellar.py.
# Sagas does not include the overlay exporter or registry override code.

from __future__ import annotations

import argparse

import copy

import dataclasses

import difflib

import json

import math

import os

from pathlib import Path

import re

import shutil

import subprocess

import sys

import tempfile

import time

import traceback

import uuid

from typing import Any, Callable, Iterable, Optional

from stellar_audio import estimate_pitch, pitch_note, process_sound, read_wav_mono, write_wav_mono

from stellar_batch import discover_projects as _discover_batch_projects

from stellar_batch import export_projects as _export_batch_projects

from stellar_shared import (
    atomic_json,
    clean_symbol,
    fbx_texture_size,
    normalize_bone,
    now_iso,
    relpath_or_abs,
    resolve_project_path,
)

from stellar_project import (
    AnimationClip,
    BoneMapping,
    Hitbox,
    SoundAsset,
    StageAsset,
    StellarProject,
    configure_project_schema,
)

from stellar_validation import validate_project as _validate_project

SCRIPT_PATH = Path(__file__).resolve()

TOOLSET_DIR = SCRIPT_PATH.parent

CUSTOM_DIR = TOOLSET_DIR.parent

GAME_DIR = CUSTOM_DIR.parent

BASE_FIGHTERS = {
    "Mario": {"symbol": "Mario", "main": 203, "motion": 202, "model": 296},
    "Fox": {"symbol": "Fox", "main": 209, "motion": 208, "model": 313},
    "Donkey Kong": {"symbol": "Donkey", "main": 213, "motion": 212, "model": 317},
    "Samus": {"symbol": "Samus", "main": 217, "motion": 216, "model": 320},
    "Luigi": {"symbol": "Luigi", "main": 221, "motion": 220, "model": 323},
    "Link": {"symbol": "Link", "main": 225, "motion": 224, "model": 324},
    "Yoshi": {"symbol": "Yoshi", "main": 247, "motion": 246, "model": 338},
    "Captain Falcon": {"symbol": "Captain", "main": 236, "motion": 235, "model": 332},
    "Kirby": {"symbol": "Kirby", "main": 229, "motion": 228, "model": 328},
    "Pikachu": {"symbol": "Pikachu", "main": 243, "motion": 242, "model": 341},
    "Jigglypuff": {"symbol": "Purin", "main": 233, "motion": 232, "model": 330},
    "Ness": {"symbol": "Ness", "main": 239, "motion": 238, "model": 335},
}

JOINT_SPECS: list[tuple[int, str, int, str]] = [
    (0, "TopN", -1, "engine"), (1, "TransN", 0, "engine"),
    (2, "XRotN", 1, "engine"), (3, "YRotN", 2, "engine"),
    (4, "Root", 3, "core"), (5, "Pelvis", 4, "spine"),
    (6, "Chest", 5, "spine"), (7, "Shoulder.R", 6, "arm_r"),
    (8, "UpperArm.R", 7, "arm_r"), (9, "Forearm.R", 8, "arm_r"),
    (10, "Hand.R", 9, "arm_r"), (11, "Neck", 6, "spine"),
    (12, "Head", 11, "spine"), (13, "Shoulder.L", 6, "arm_l"),
    (14, "UpperArm.L", 13, "arm_l"), (15, "Forearm.L", 14, "arm_l"),
    (16, "Hand.L", 15, "arm_l"), (17, "ItemHold", 16, "accessory"),
    (18, "Hip.R", 5, "leg_r"), (19, "Thigh.R", 18, "leg_r"),
    (20, "Shin.R", 19, "leg_r"), (21, "Ankle.R", 20, "leg_r"),
    (22, "Foot.R", 21, "leg_r"), (23, "Hip.L", 5, "leg_l"),
    (24, "Thigh.L", 23, "leg_l"), (25, "Shin.L", 24, "leg_l"),
    (26, "Ankle.L", 25, "leg_l"), (27, "Foot.L", 26, "leg_l"),
    (28, "ThrowN", 4, "accessory"),
]

BASE_RUNTIME_JOINTS: dict[str, dict[int, int]] = {
    "Mario": {joint: joint for joint in range(4, 29)},
    "Fox": {**{joint: joint for joint in range(4, 28)}, 28: 30},
    # Captain has an extra body helper immediately before the legs.  Its
    # light-item joint remains 17, but both leg chains and ThrowN are one slot
    # later than the canonical Mario interchange rig.
    "Captain Falcon": {
        **{joint: joint for joint in range(4, 18)},
        18: 19, 19: 20, 20: 21, 21: 22, 22: 23,
        23: 24, 24: 25, 25: 26, 26: 27, 27: 28, 28: 29,
    },
    # Kirby's setup mask omits the humanoid shoulder/head helper slots at
    # 7, 12, 18, and 19. His visible right arm starts at 8, the left arm at
    # 13, and the two five-link leg chains at 20 and 25. Kirby's round body
    # at joint 6 supplies the central chest/neck/head motion. Falling back to
    # Mario's numeric ABI binds a human proxy's right arm to an omitted DObj
    # and its legs to Kirby's body helpers, stretching the mesh across the CSS.
    "Kirby": {
        4: 4, 5: 5, 6: 6,
        7: 8, 8: 9, 9: 10, 10: 11,
        11: 6, 12: 6,
        13: 13, 14: 14, 15: 15, 16: 16, 17: 17,
        18: 20, 19: 21, 20: 22, 21: 23, 22: 24,
        23: 25, 24: 26, 25: 27, 26: 28, 27: 29,
        # Kirby has no separately-created ThrowN in his normal setup mask.
        28: 6,
    },
    # Purin/Jigglypuff uses Kirby's compact body topology: each arm has an
    # extra root before its visible three-link chain and the legs/ThrowN are
    # shifted by one.  Purin has no live humanoid neck/head DObjs: setup_parts
    # deliberately omits joints 7 and 18, so collapse both onto the animated
    # body at joint 6.  Treating these IDs as Mario joints drives the head with
    # an arm or the pelvis and attaches both legs to unrelated helpers.
    "Jigglypuff": {
        4: 4, 5: 5, 6: 6,
        7: 8, 8: 9, 9: 10, 10: 11,
        11: 6, 12: 6,
        13: 12, 14: 13, 15: 14, 16: 15, 17: 16,
        18: 19, 19: 20, 20: 21, 21: 22, 22: 23,
        23: 24, 24: 25, 25: 26, 26: 27, 27: 28, 28: 29,
    },
    # Pikachu inserts two ear/detail nodes before its left arm and an extra
    # tail node before ThrowN. Its legs therefore start one slot later than
    # Mario's, while both imported Neck and Head belong on the animated body
    # root at 11.
    "Pikachu": {
        4: 4, 5: 5, 6: 6,
        7: 7, 8: 8, 9: 9, 10: 10,
        11: 11, 12: 11,
        13: 15, 14: 16, 15: 17, 16: 18, 17: 12,
        18: 19, 19: 20, 20: 21, 21: 22, 22: 23,
        23: 24, 24: 25, 25: 26, 26: 27, 27: 28, 28: 30,
    },
    # Link has helper/accessory DObjs between his visible limb chains.  In
    # particular, the Mario-semantic head ID is Link's left shoulder and the
    # Mario-semantic legs land in Link's sword/head helpers.  Keep those
    # helpers in the inherited tree, but attach imported geometry to the live
    # body joints that Link's AnimJoint streams actually drive.
    "Link": {
        4: 4, 5: 5, 6: 6,
        7: 7, 8: 8, 9: 9, 10: 10,
        11: 22, 12: 23,
        13: 12, 14: 13, 15: 14, 16: 15, 17: 16,
        # Link's first leg branch starts at runtime joint 25 (positive-X,
        # matching Mario's first/canonical R chain) and the second starts at
        # 30.  joint_rfoot_id/joint_lfoot_id name the collision endpoints in
        # the opposite order and must not be used as a skeleton topology map;
        # doing so crossed the imported legs and left one apparently missing.
        18: 25, 19: 26, 20: 27, 21: 28, 22: 29,
        23: 30, 24: 31, 25: 32, 26: 33, 27: 34, 28: 35,
    },
    "Donkey Kong": {
        **{joint: joint for joint in range(4, 18)},
        18: 19, 19: 20, 20: 21, 21: 22, 22: 23,
        23: 24, 24: 25, 25: 26, 26: 27, 27: 28, 28: 29,
    },
    "Yoshi": {
        4: 4, 5: 5, 6: 6,
        7: 10, 8: 11, 9: 12, 10: 13,
        11: 7, 12: 8,
        13: 14, 14: 15, 15: 16, 16: 17, 17: 18,
        18: 21, 19: 22, 20: 23, 21: 24, 22: 25,
        23: 26, 24: 27, 25: 28, 26: 29, 27: 30, 28: 31,
    },
    # Samus inserts a cannon/item joint after her right hand, moves the
    # neck/head one slot later, reserves joints 18-25 for grapple/cannon
    # helpers, and starts her legs at 26.  Those helpers are deliberately
    # absent from the normal setup mask, so attaching a canonical limb to the
    # Mario-numbered joint either leaves it unanimated or collapses it onto an
    # unrelated ancestor.
    "Samus": {
        4: 4, 5: 5, 6: 6,
        7: 7, 8: 8, 9: 9, 10: 10,
        11: 12, 12: 13,
        13: 14, 14: 15, 15: 16, 16: 17, 17: 11,
        18: 26, 19: 27, 20: 28, 21: 29, 22: 30,
        23: 31, 24: 32, 25: 33, 26: 34, 27: 35, 28: 36,
    },
}

RUNTIME_JOINT_LAYOUT_VERSION = 7

LINK_RUNTIME_JOINTS_V3_TO_V6: dict[int, int] = {
    4: 4, 5: 5, 6: 6,
    7: 7, 8: 8, 9: 9, 10: 10,
    11: 22, 12: 23,
    13: 12, 14: 13, 15: 14, 16: 15, 17: 16,
    18: 30, 19: 31, 20: 32, 21: 33, 22: 34,
    23: 25, 24: 26, 25: 27, 26: 28, 27: 29, 28: 35,
}

def runtime_joint_id(base: str, semantic_joint: int) -> int:
    return BASE_RUNTIME_JOINTS.get(base, BASE_RUNTIME_JOINTS["Mario"]).get(
        semantic_joint, semantic_joint)

def runtime_layout_needs_remap(base: str, stored_version: int) -> bool:
    """Return whether a stored base-runtime proxy predates its base mapping."""
    return ((base in {"Captain Falcon", "Jigglypuff"} and stored_version < 2) or
            (base == "Link" and stored_version < 7) or
            (base == "Pikachu" and stored_version < 5) or
            (base == "Kirby" and stored_version < 6))

def migrate_runtime_joint_id(base: str, runtime_joint: int,
                             stored_version: int) -> int:
    """Translate one cached runtime ID through its historical semantic ID."""
    if base == "Link" and 3 <= stored_version < 7:
        semantic_by_runtime = {
            old_runtime: semantic
            for semantic, old_runtime in LINK_RUNTIME_JOINTS_V3_TO_V6.items()
        }
        semantic = semantic_by_runtime.get(runtime_joint, runtime_joint)
        return runtime_joint_id(base, semantic)
    # Layouts older than a fighter's first runtime-specific map contain the
    # canonical semantic IDs and follow the original migration path.
    return runtime_joint_id(base, runtime_joint)

def mapped_runtime_parents(base: str, mappings: Iterable[Any]) -> dict[int, int]:
    """Collapse non-deforming helper joints to the real imported hierarchy."""
    semantic_parents = {joint: parent for joint, _name, parent, _chain in JOINT_SPECS}
    mapped = {
        int(mapping.game_id)
        for mapping in mappings
        if bool(getattr(mapping, "enabled", True)) and bool(getattr(mapping, "source", ""))
    }
    result: dict[int, int] = {}
    # Compact skeletons can map several semantic pivots to one runtime DObj.
    # Process from the central/earlier pivot outward and preserve its parent;
    # a later collapsed neck/head mapping must never turn the shared DObj into
    # its own parent (which stretches every weighted vertex toward the origin).
    for semantic_joint in sorted(mapped):
        parent = semantic_parents.get(semantic_joint, 4)
        while parent >= 5 and parent not in mapped:
            parent = semantic_parents.get(parent, 4)
        runtime_joint = runtime_joint_id(base, semantic_joint)
        runtime_parent = runtime_joint_id(base, parent) if parent >= 4 else 4
        if runtime_parent == runtime_joint:
            continue
        result.setdefault(runtime_joint, runtime_parent)
    return result

REST_POSITIONS = {
    0: (0.0, 0.0, 0.0), 1: (0.0, 0.0, 0.0), 2: (0.0, 0.0, 0.0),
    3: (0.0, 0.0, 0.0), 4: (0.0, 150.0, 0.0), 5: (0.0, 0.0, 0.0),
    6: (0.0, 0.0, 0.0), 7: (51.06, 53.75, 0.0),
    8: (0.0, 0.0, 0.0), 9: (40.34, 0.0, 0.0), 10: (35.97, 0.0, 0.0),
    11: (0.0, 72.07, 0.0), 12: (0.0, 13.51, -4.5),
    13: (-51.06, 53.75, 0.0), 14: (0.0, 0.0, 0.0),
    15: (40.33, 0.0, 0.0), 16: (35.99, 0.0, 0.0),
    17: (35.06, -17.55, 3.52), 18: (31.68, -20.65, -3.0),
    19: (0.0, 0.0, 0.0), 20: (55.58, 0.0, 0.0),
    21: (66.11, 1.36, 2.32), 22: (0.0, 0.0, 0.0),
    23: (-31.68, -20.65, -3.0), 24: (0.0, 0.0, 0.0),
    25: (55.58, 0.0, 0.0), 26: (65.81, 1.31, 2.32),
    27: (0.0, 0.0, 0.0), 28: (0.0, 0.0, 120.0),
}

REST_ROTATIONS = {
    4: (0.0, 0.0, 0.0), 5: (0.0, 0.0, 0.0), 6: (0.0, 0.0, 0.0),
    7: (-1.5707960129, 0.0, -1.5707960129), 8: (-0.1028010026, -0.4999369979, 0.0),
    9: (0.0, 0.0, 0.0), 10: (0.0, 0.0, 0.0), 11: (0.0, 0.0, 0.0),
    12: (0.0, 0.0, 0.0), 13: (-1.5707960129, 0.0, -1.5707960129),
    14: (0.1073520035, 0.4990569949, 0.0095020002), 15: (0.0, 0.0, -0.0178050008),
    16: (0.0, 0.0, 0.0), 17: (0.0, 0.0, 0.0),
    18: (-1.5707960129, 0.0, -1.5707960129), 19: (0.1114299968, -0.0833450034, -0.0349799991),
    20: (0.0, 0.0, 0.0), 21: (0.0, -0.000009, -1.6418110132),
    22: (-0.0752729997, -0.4641610086, 0.2130469978),
    23: (-1.5707960129, 0.0, -1.5707960129), 24: (-0.1091170013, 0.0833790004, -0.0188250002),
    25: (0.0, 0.0, 0.0), 26: (0.0, 0.000009, -1.6418240070),
    27: (0.0305780005, 0.4896720052, 0.1966809928), 28: (0.0, 0.0, 0.0),
}

Mat3 = tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]

IDENTITY_MAT3: Mat3 = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))

REST_DIRECTION_EPSILON_SQ = 1.0e-6

def _is_meaningful_rest_direction(values: Iterable[float]) -> bool:
    return sum(float(value) * float(value) for value in values) > REST_DIRECTION_EPSILON_SQ

def _mat3_mul(left: Mat3, right: Mat3) -> Mat3:
    return tuple(tuple(sum(left[row][k] * right[k][column] for k in range(3))
                       for column in range(3)) for row in range(3))

def _mat3_transpose(matrix: Mat3) -> Mat3:
    return tuple(tuple(matrix[column][row] for column in range(3))
                 for row in range(3))

def _mat3_vec(matrix: Mat3, vector: Iterable[float]) -> tuple[float, float, float]:
    x, y, z = (float(value) for value in vector)
    return tuple(matrix[row][0] * x + matrix[row][1] * y + matrix[row][2] * z
                 for row in range(3))

def _mat3_transpose_vec(matrix: Mat3, vector: Iterable[float]) -> tuple[float, float, float]:
    """Apply the inverse of a rotation matrix without building an inverse."""
    x, y, z = (float(value) for value in vector)
    return tuple(matrix[0][column] * x + matrix[1][column] * y + matrix[2][column] * z
                 for column in range(3))

def _euler_xyz_matrix(rotation: Iterable[float]) -> Mat3:
    """Match the game/Blender XYZ Euler transform used by the joint tree."""
    x, y, z = (float(value) for value in rotation)
    cx, sx = math.cos(x), math.sin(x)
    cy, sy = math.cos(y), math.sin(y)
    cz, sz = math.cos(z), math.sin(z)
    rotate_x: Mat3 = ((1.0, 0.0, 0.0), (0.0, cx, -sx), (0.0, sx, cx))
    rotate_y: Mat3 = ((cy, 0.0, sy), (0.0, 1.0, 0.0), (-sy, 0.0, cy))
    rotate_z: Mat3 = ((cz, -sz, 0.0), (sz, cz, 0.0), (0.0, 0.0, 1.0))
    return _mat3_mul(rotate_z, _mat3_mul(rotate_y, rotate_x))

def game_world_transforms(game_pose: Optional[dict[int, dict[str, list[float]]]] = None
                          ) -> tuple[dict[int, tuple[float, float, float]], dict[int, Mat3]]:
    """Resolve canonical local joint data through the complete parent chain.

    AnimJoint tracks are absolute local values. Unanimated axes retain the
    model's rest values, which is essential for Mario's rotated shoulder and
    leg pivots and for joint-local hitbox offsets.
    """
    pose = game_pose or {}
    positions: dict[int, tuple[float, float, float]] = {3: (0.0, 0.0, 0.0)}
    rotations: dict[int, Mat3] = {3: IDENTITY_MAT3}
    for game_id, _name, parent, _chain in JOINT_SPECS:
        if game_id < 4:
            continue
        values = pose.get(game_id, {})
        translation = values.get("translation", list(REST_POSITIONS.get(game_id, (0.0, 0.0, 0.0))))
        rotation = values.get("rotation", list(REST_ROTATIONS.get(game_id, (0.0, 0.0, 0.0))))
        parent_position = positions.get(parent, (0.0, 0.0, 0.0))
        parent_rotation = rotations.get(parent, IDENTITY_MAT3)
        translated = _mat3_vec(parent_rotation, translation)
        positions[game_id] = tuple(parent_position[i] + translated[i] for i in range(3))
        rotations[game_id] = _mat3_mul(parent_rotation, _euler_xyz_matrix(rotation))
    return positions, rotations

def base_paths(base: str) -> tuple[Optional[Path], Optional[Path], Optional[Path]]:
    info = BASE_FIGHTERS.get(base, BASE_FIGHTERS["Mario"])
    reloc = GAME_DIR / "src" / "relocData"
    symbol = info["symbol"]
    main = reloc / f"{info['main']}_{symbol}Main.c"
    motion = reloc / f"{info['motion']}_{symbol}MainMotion.c"
    model_candidates = sorted(reloc.glob(f"{info['model']}_*Model.c"))
    return (main if main.exists() else None, motion if motion.exists() else None,
            model_candidates[0] if model_candidates else None)

def load_base_joint_tree(base: str) -> list[dict[str, Any]]:
    """Read the selected fighter's high-detail DObj topology and bind pose.

    Fighter setup masks are authored against these exact descriptor depths.
    Reusing Mario's topology for another fighter can leave a skipped parent
    followed by a live child, which makes lbCommonSetupFighterPartsDObjs pass
    a null parent to gcAddChildForDObj.
    """
    _main, _motion, model = base_paths(base)
    if model is None:
        return []
    symbol = BASE_FIGHTERS.get(base, BASE_FIGHTERS["Mario"])["symbol"]
    text = model.read_text(encoding="utf-8", errors="replace")
    marker = re.search(rf"DObjDesc\s+d{re.escape(symbol)}Model_JointTree\s*\[\s*\]\s*=\s*\{{", text)
    if marker is None:
        return []
    end = text.find("\n};", marker.end())
    if end < 0:
        return []
    entry_re = re.compile(
        r"\{\s*([^,{}]+)\s*,\s*\(void\*\)[^,{}]+,\s*"
        r"\{\s*([^{}]+)\}\s*,\s*\{\s*([^{}]+)\}\s*,\s*"
        r"\{\s*([^{}]+)\}\s*\}"
    )

    def vector(payload: str) -> tuple[float, float, float]:
        values = [float(value.strip().rstrip("fF")) for value in payload.split(",")]
        if len(values) != 3:
            raise ValueError("DObj vector does not have three components")
        return (values[0], values[1], values[2])

    result: list[dict[str, Any]] = []
    latest_at_depth: dict[int, int] = {-1: 3}
    for match in entry_re.finditer(text[marker.end():end]):
        depth_text, translation_text, rotation_text, scale_text = match.groups()
        depth = 18 if depth_text.strip() == "DOBJ_ARRAY_MAX" else int(depth_text.strip(), 0)
        if depth == 18:
            break
        game_id = 4 + len(result)
        parent = latest_at_depth.get(depth - 1, 3)
        result.append({
            "game_id": game_id,
            "depth": depth,
            "parent": parent,
            "translation": vector(translation_text),
            "rotation": vector(rotation_text),
            "scale": vector(scale_text),
        })
        latest_at_depth[depth] = game_id
        for stale_depth in [value for value in latest_at_depth if value > depth]:
            del latest_at_depth[stale_depth]
    return result

def base_game_world_transforms(base: str,
                               game_pose: Optional[dict[int, dict[str, list[float]]]] = None) -> tuple[
        dict[int, tuple[float, float, float]], dict[int, Mat3]]:
    """Resolve a base fighter's actual local DObj tree into world space."""
    tree = load_base_joint_tree(base)
    if not tree:
        return game_world_transforms()
    positions: dict[int, tuple[float, float, float]] = {3: (0.0, 0.0, 0.0)}
    rotations: dict[int, Mat3] = {3: IDENTITY_MAT3}
    pose = game_pose or {}
    for entry in tree:
        game_id = int(entry["game_id"])
        parent = int(entry["parent"])
        values = pose.get(game_id, {})
        translation = values.get("translation", entry["translation"])
        rotation = values.get("rotation", entry["rotation"])
        parent_position = positions.get(parent, (0.0, 0.0, 0.0))
        parent_rotation = rotations.get(parent, IDENTITY_MAT3)
        translated = _mat3_vec(parent_rotation, translation)
        positions[game_id] = tuple(parent_position[i] + translated[i] for i in range(3))
        rotations[game_id] = _mat3_mul(parent_rotation, _euler_xyz_matrix(rotation))
    return positions, rotations

def prepare_rigid_for_base(project: StellarProject, payload: dict[str, Any]) -> dict[str, Any]:
    """Move a canonical FBX proxy into the selected fighter's runtime ABI.

    Existing exports store Mario-semantic joint numbers and joint-local vectors.
    Reusing those numbers against Donkey/Yoshi DObjs either animates the wrong
    body part or stretches the mesh between unrelated pivots.  Preserve the
    source mesh in world space while changing IDs and re-expressing all local
    vectors in the selected base fighter's bind frames.  The visual multiplier
    is applied here as a vertex bake, independently of FTAttributes.size.
    """
    rigid = copy.deepcopy(payload)
    base_positions, base_rotations = base_game_world_transforms(project.base_character)
    canonical_positions, canonical_rotations = game_world_transforms()
    runtime_space = str(rigid.get("joint_space", "")) == "base-runtime"
    stored_layout_version = int(rigid.get("runtime_joint_layout", 0) or 0)
    # Older proxies were labelled as base-runtime before their fighter-specific
    # joint ABI was known. Their local coordinates are in the old base frames,
    # so migrate IDs/frames once without applying canonical-space assumptions.
    needs_layout_remap = (
        runtime_space and
        runtime_layout_needs_remap(project.base_character, stored_layout_version)
    )
    already_runtime = runtime_space and not needs_layout_remap
    old_positions = base_positions if runtime_space else canonical_positions
    old_rotations = base_rotations if runtime_space else canonical_rotations

    def remap(joint: int) -> int:
        if already_runtime:
            return joint
        if runtime_space:
            return migrate_runtime_joint_id(
                project.base_character, joint, stored_layout_version)
        return runtime_joint_id(project.base_character, joint)

    raw_bind = {
        int(joint): tuple(float(value) for value in position)
        for joint, position in dict(rigid.get("bind_joints") or {}).items()
    }
    old_pelvis = raw_bind.get(5, old_positions.get(5, (0.0, 0.0, 0.0)))
    new_pelvis = base_positions.get(5, (0.0, 0.0, 0.0))
    baked_scale = max(1.0e-6, float(rigid.get("baked_model_scale", 1.0)))
    visual_factor = float(project.game_model_scale) / baked_scale

    # Scale the visible mesh from the soles instead of from the pelvis.  The
    # latter makes a larger proxy grow equally upward and downward, so its
    # feet sink below the CSS pedestal and the stage floor.  Preserve the
    # visual 1.0 floor placement while keeping X/Z centred on the pelvis.
    old_floor = old_pelvis[1]
    geometry_floor: list[float] = []
    for triangle in rigid.get("geometry", []):
        attach = int(triangle.get("joint", 5))
        attach_position = old_positions.get(attach, old_positions[5])
        attach_rotation = old_rotations.get(attach, IDENTITY_MAT3)
        for vertex in triangle.get("vertices", []):
            offset = _mat3_vec(attach_rotation, vertex.get("position", (0.0, 0.0, 0.0)))
            geometry_floor.append(attach_position[1] + offset[1])
    if geometry_floor:
        old_floor = min(geometry_floor)
    # A Blender-produced base-runtime proxy is already fitted around its own
    # source pivots. Re-anchoring that proxy's pelvis to the base fighter here
    # undoes the floor-based scale bake (and put some characters' waist on the
    # CSS pedestal). Canonical legacy payloads still need the old relocation;
    # runtime payloads only need an incremental visual scale around their
    # existing sole plane.
    if runtime_space:
        scale_center = old_pelvis
        new_floor = old_floor
    else:
        scale_center = new_pelvis
        new_floor = new_pelvis[1] + old_floor - old_pelvis[1]

    def transform_point(point: Iterable[float]) -> tuple[float, float, float]:
        values = tuple(float(value) for value in point)
        return (
            scale_center[0] + (values[0] - old_pelvis[0]) * visual_factor,
            new_floor + (values[1] - old_floor) * visual_factor,
            scale_center[2] + (values[2] - old_pelvis[2]) * visual_factor,
        )

    bind_joints: dict[int, tuple[float, float, float]] = {}
    for old_joint, point in raw_bind.items():
        # Compact bases can collapse multiple semantic pivots onto one live
        # DObj. Keep the first (most central) source pivot and express the
        # later neck/head vertices around it instead of letting the last
        # semantic joint silently replace the shared bind origin.
        bind_joints.setdefault(remap(old_joint), transform_point(point))
    for entry in load_base_joint_tree(project.base_character):
        joint = int(entry["game_id"])
        bind_joints.setdefault(joint, base_positions[joint])

    # Imported characters conventionally arrive in a T-pose.  Animation
    # deltas alone preserve that T-pose forever, because the game's descriptor
    # rotations are its bind posture rather than an animation frame.  Build a
    # roll-stable source frame and target frame from the actual mapped bone
    # directions, then pre-pose the mesh into the base fighter's bind posture
    # while retaining every imported segment length.
    source_parents = mapped_runtime_parents(project.base_character, project.bone_mappings)
    runtime_chains = {
        runtime_joint_id(project.base_character, int(mapping.game_id)): mapping.chain
        for mapping in project.bone_mappings
        if mapping.enabled and bool(mapping.source)
    }
    source_children: dict[int, list[int]] = {}
    for child, parent in source_parents.items():
        source_children.setdefault(parent, []).append(child)

    def segment_direction(points: dict[int, tuple[float, float, float]],
                          joint: int) -> tuple[float, float, float]:
        chain = runtime_chains.get(joint, "")
        queue = sorted(source_children.get(joint, []),
                       key=lambda child: runtime_chains.get(child, "") != chain)
        visited: set[int] = set()
        while queue:
            child = queue.pop(0)
            if child in visited:
                continue
            visited.add(child)
            if child in points and joint in points:
                delta = tuple(points[child][axis] - points[joint][axis] for axis in range(3))
                if _is_meaningful_rest_direction(delta):
                    return delta
            descendants = sorted(source_children.get(child, []),
                                 key=lambda item: runtime_chains.get(item, "") != chain)
            queue[0:0] = descendants
        parent = source_parents.get(joint, 5)
        if parent in points and joint in points:
            delta = tuple(points[joint][axis] - points[parent][axis] for axis in range(3))
            if _is_meaningful_rest_direction(delta):
                return delta
        return (0.0, 1.0, 0.0)

    def vector_frame(primary: tuple[float, float, float]) -> Mat3:
        magnitude = math.sqrt(sum(value * value for value in primary)) or 1.0
        primary = tuple(value / magnitude for value in primary)
        forward = (0.0, 0.0, 1.0)
        projection = sum(primary[axis] * forward[axis] for axis in range(3))
        forward = tuple(forward[axis] - primary[axis] * projection for axis in range(3))
        magnitude = math.sqrt(sum(value * value for value in forward))
        if magnitude < 1.0e-5:
            forward = (1.0, 0.0, 0.0)
            projection = sum(primary[axis] * forward[axis] for axis in range(3))
            forward = tuple(forward[axis] - primary[axis] * projection for axis in range(3))
            magnitude = math.sqrt(sum(value * value for value in forward)) or 1.0
        forward = tuple(value / magnitude for value in forward)
        right = (
            primary[1] * forward[2] - primary[2] * forward[1],
            primary[2] * forward[0] - primary[0] * forward[2],
            primary[0] * forward[1] - primary[1] * forward[0],
        )
        # Basis vectors are columns: X=right, Y=bone direction, Z=forward.
        return tuple((right[row], primary[row], forward[row])
                     for row in range(3))  # type: ignore[return-value]

    def direction_frame(points: dict[int, tuple[float, float, float]], joint: int) -> Mat3:
        return vector_frame(segment_direction(points, joint))

    # DK's descriptor has a short lower-spine segment bending backward and a
    # much longer upper segment returning forward. Mapping a human rig's long
    # first spine bone onto only that short segment magnifies the backward
    # portion and leaves the whole proxy arched the wrong way. Use DK's complete
    # pelvis-to-head direction as the rest guide for each imported spine link;
    # the inherited animation matrices still supply DK's actual moving hunch.
    dk_spine_joints = {
        runtime_joint_id(project.base_character, semantic)
        for semantic in (5, 6, 11)
    }
    dk_head = runtime_joint_id(project.base_character, 12)
    dk_pelvis = runtime_joint_id(project.base_character, 5)
    dk_spine_direction = tuple(
        base_positions.get(dk_head, base_positions[dk_pelvis])[axis] -
        base_positions[dk_pelvis][axis]
        for axis in range(3)
    )

    rest_corrections: dict[int, Mat3] = {}
    preserve_clavicle_frames = {
        runtime_joint_id(project.base_character, semantic)
        for semantic in (7, 13)
    } if project.base_character in {"Kirby", "Pikachu", "Jigglypuff"} else set()
    for joint in source_parents:
        if joint in preserve_clavicle_frames:
            # Kirby, Pikachu, and Purin use compact, non-humanoid shoulder chains.
            # Their base shoulder-to-arm vectors point mostly through Y/Z;
            # using those vectors as a humanoid clavicle rest frame removes
            # most of the clavicle's horizontal span and pinches the chest.
            # Keep the imported clavicle frame; child arm joints still inherit
            # the base animation and receive their normal rest correction.
            rest_corrections[joint] = IDENTITY_MAT3
            continue
        elif (project.base_character in {"Kirby", "Jigglypuff"} and
                joint == runtime_joint_id(project.base_character, 6)):
            # Neck and Head collapse onto the round fighter's body joint. With
            # no central child left, generic direction selection picks the first
            # arm and rotates the complete torso/head toward a shoulder. Use the
            # pelvis-to-body axis for this one compact central joint.
            source_axis = tuple(
                bind_joints[joint][axis] - bind_joints[5][axis]
                for axis in range(3)
            )
            target_axis = tuple(
                base_positions[joint][axis] - base_positions[5][axis]
                for axis in range(3)
            )
            source_frame = vector_frame(source_axis)
            target_frame = vector_frame(target_axis)
        else:
            source_frame = direction_frame(bind_joints, joint)
            target_frame = direction_frame(base_positions, joint)
        if project.base_character == "Donkey Kong" and joint in dk_spine_joints:
            target_frame = vector_frame(dk_spine_direction)
        rest_corrections[joint] = _mat3_mul(target_frame, _mat3_transpose(source_frame))

    posed_bind = dict(bind_joints)
    posed_bind[5] = bind_joints.get(5, base_positions[5])
    completed = {5}
    pending = dict(source_parents)
    pending.pop(5, None)
    while pending:
        progressed = False
        for joint, parent in list(pending.items()):
            if parent not in completed or joint not in bind_joints or parent not in bind_joints:
                continue
            segment = tuple(bind_joints[joint][axis] - bind_joints[parent][axis]
                            for axis in range(3))
            rotated = _mat3_vec(rest_corrections.get(parent, IDENTITY_MAT3), segment)
            posed_bind[joint] = tuple(posed_bind[parent][axis] + rotated[axis]
                                       for axis in range(3))
            completed.add(joint)
            pending.pop(joint)
            progressed = True
        if not progressed:
            break

    for triangle in rigid.get("geometry", []):
        old_attach = int(triangle.get("joint", 5))
        new_attach = remap(old_attach)
        old_attach_position = old_positions.get(old_attach, old_positions[5])
        old_attach_rotation = old_rotations.get(old_attach, IDENTITY_MAT3)
        new_attach_position = base_positions.get(new_attach, base_positions[5])
        new_attach_rotation = base_rotations.get(new_attach, IDENTITY_MAT3)
        for vertex in triangle.get("vertices", []):
            old_world_offset = _mat3_vec(old_attach_rotation,
                                          vertex.get("position", (0.0, 0.0, 0.0)))
            old_world = tuple(old_attach_position[axis] + old_world_offset[axis]
                              for axis in range(3))
            new_world = transform_point(old_world)
            influences = list(vertex.get("skin", []))
            if not influences:
                influences = [{"joint": old_attach, "weight": 1.0}]
                vertex["skin"] = influences
            posed_world = [0.0, 0.0, 0.0]
            total_weight = 0.0
            for influence in influences:
                old_joint = int(influence.get("joint", old_attach))
                new_joint = remap(old_joint)
                weight = float(influence.get("weight", 0.0))
                source_origin = bind_joints.get(
                    new_joint, base_positions.get(new_joint, base_positions[5]))
                target_origin = posed_bind.get(new_joint, source_origin)
                source_relative = tuple(new_world[axis] - source_origin[axis]
                                        for axis in range(3))
                corrected = _mat3_vec(
                    rest_corrections.get(new_joint, IDENTITY_MAT3), source_relative)
                for axis in range(3):
                    posed_world[axis] += weight * (target_origin[axis] + corrected[axis])
                total_weight += weight
            if total_weight <= 1.0e-8:
                posed_world = list(new_world)
            else:
                posed_world = [value / total_weight for value in posed_world]
            new_relative = tuple(posed_world[axis] - new_attach_position[axis]
                                 for axis in range(3))
            vertex["position"] = [round(value, 4) for value in
                                  _mat3_transpose_vec(new_attach_rotation, new_relative)]
            for influence in influences:
                old_joint = int(influence.get("joint", old_attach))
                new_joint = remap(old_joint)
                new_rotation = base_rotations.get(new_joint, IDENTITY_MAT3)
                source_origin = posed_bind.get(
                    new_joint, base_positions.get(new_joint, base_positions[5]))
                influence_relative = tuple(
                    posed_world[axis] - source_origin[axis]
                    for axis in range(3)
                )
                influence["joint"] = new_joint
                influence["position"] = [round(value, 4) for value in
                                         _mat3_transpose_vec(new_rotation, influence_relative)]
        triangle["joint"] = new_attach

    # Rest-pose retargeting rotates the legs after the visual scale bake, so
    # its lowest point can move away from the fighter-local floor. Apply one
    # final visual-only translation to the complete prepared proxy. Runtime
    # position, collision, hitboxes, and FTAttributes.size remain untouched.
    prepared_floor = float("inf")
    for triangle in rigid.get("geometry", []):
        attach = int(triangle.get("joint", 5))
        attach_position = base_positions.get(attach, base_positions[5])
        attach_rotation = base_rotations.get(attach, IDENTITY_MAT3)
        for vertex in triangle.get("vertices", []):
            offset = _mat3_vec(attach_rotation,
                               vertex.get("position", (0.0, 0.0, 0.0)))
            prepared_floor = min(prepared_floor, attach_position[1] + offset[1])
    floor_delta = -prepared_floor if math.isfinite(prepared_floor) else 0.0
    if abs(floor_delta) > 1.0e-5:
        for triangle in rigid.get("geometry", []):
            attach = int(triangle.get("joint", 5))
            attach_position = base_positions.get(attach, base_positions[5])
            attach_rotation = base_rotations.get(attach, IDENTITY_MAT3)
            for vertex in triangle.get("vertices", []):
                offset = _mat3_vec(attach_rotation,
                                   vertex.get("position", (0.0, 0.0, 0.0)))
                world = (attach_position[0] + offset[0],
                         attach_position[1] + offset[1] + floor_delta,
                         attach_position[2] + offset[2])
                relative = tuple(world[axis] - attach_position[axis]
                                 for axis in range(3))
                vertex["position"] = [round(value, 4) for value in
                                      _mat3_transpose_vec(attach_rotation, relative)]
        for joint in posed_bind:
            point = posed_bind[joint]
            posed_bind[joint] = (point[0], point[1] + floor_delta, point[2])

    bind_joints.update(posed_bind)
    rigid["bind_joints"] = {
        str(joint): [round(value, 4) for value in point]
        for joint, point in sorted(bind_joints.items())
    }
    rigid["joint_space"] = "base-runtime"
    rigid["runtime_joint_layout"] = RUNTIME_JOINT_LAYOUT_VERSION
    rigid["base_skeleton"] = project.base_character
    rigid["baked_model_scale"] = float(project.game_model_scale)
    rigid["fit_scale"] = float(rigid.get("fit_scale", 1.0)) * visual_factor
    return rigid

GAME_DIR = Path(__file__).resolve().parents[3] / 'ssb-decomp-re'
