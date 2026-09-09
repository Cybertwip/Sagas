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

sys.path.insert(0, str(Path(__file__).resolve().parent))

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

def runtime_joint_id(base: str, semantic_joint: int) -> int:
    return BASE_RUNTIME_JOINTS.get(base, BASE_RUNTIME_JOINTS["Mario"]).get(
        semantic_joint, semantic_joint)

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

def _mat3_mul(left: Mat3, right: Mat3) -> Mat3:
    return tuple(tuple(sum(left[row][k] * right[k][column] for k in range(3))
                       for column in range(3)) for row in range(3))

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

def normalize_rigid_skin_scale(rigid: dict[str, Any]) -> float:
    """Guard against FBX unit scales leaking into target-joint bind locals."""
    geometry = rigid.get("geometry", [])
    if not geometry or not any(vertex.get("skin") for triangle in geometry for vertex in triangle.get("vertices", [])):
        return 1.0
    # Source-pivot meshes have already been normalized explicitly during the
    # Blender bake.  Their locals are offsets from Mia's own bind pivots, not
    # offsets from Mario's bind joints, so the legacy target-skeleton extent
    # heuristic would measure the wrong space and deform an otherwise valid
    # pass-through mesh.
    if str(rigid.get("skinning", {}).get("method", "")).startswith("source-pivot"):
        return 1.0
    bind_positions, bind_rotations = game_world_transforms()
    minimum = [float("inf")] * 3; maximum = [float("-inf")] * 3
    for triangle in geometry:
        for vertex in triangle.get("vertices", []):
            world = [0.0, 0.0, 0.0]; total = 0.0
            for influence in vertex.get("skin", []):
                joint = int(influence.get("joint", 5)); weight = float(influence.get("weight", 0.0))
                offset = _mat3_vec(bind_rotations.get(joint, IDENTITY_MAT3), influence.get("position", (0.0, 0.0, 0.0)))
                base = bind_positions.get(joint, bind_positions[5])
                for axis in range(3): world[axis] += weight * (base[axis] + offset[axis])
                total += weight
            if total:
                for axis in range(3):
                    value = world[axis] / total
                    minimum[axis] = min(minimum[axis], value); maximum[axis] = max(maximum[axis], value)
    extent = max((maximum[axis] - minimum[axis] for axis in range(3)), default=0.0)
    skeleton_extent = max(max(values[axis] for values in bind_positions.values()) -
                          min(values[axis] for values in bind_positions.values()) for axis in range(3))
    if not math.isfinite(extent) or extent <= skeleton_extent * 4.0:
        return 1.0
    factor = skeleton_extent * 1.5 / extent
    for triangle in geometry:
        attached = int(triangle.get("joint", 5))
        attached_base = bind_positions.get(attached, bind_positions[5])
        attached_rotation = bind_rotations.get(attached, IDENTITY_MAT3)
        for vertex in triangle.get("vertices", []):
            world = [0.0, 0.0, 0.0]; total = 0.0
            for influence in vertex.get("skin", []):
                influence["position"] = [round(float(value) * factor, 4)
                                         for value in influence.get("position", (0.0, 0.0, 0.0))]
                joint = int(influence.get("joint", attached)); weight = float(influence.get("weight", 0.0))
                offset = _mat3_vec(bind_rotations.get(joint, IDENTITY_MAT3), influence["position"])
                base = bind_positions.get(joint, bind_positions[5])
                for axis in range(3): world[axis] += weight * (base[axis] + offset[axis])
                total += weight
            if total:
                world = [value / total for value in world]
                relative = [world[axis] - attached_base[axis] for axis in range(3)]
                vertex["position"] = [round(value, 4) for value in _mat3_transpose_vec(attached_rotation, relative)]
    rigid.setdefault("skinning", {})["unit_scale_correction"] = factor
    return factor

SYNONYMS = {
    "root": ("root", "reference", "master", "armature"),
    "pelvis": ("pelvis", "hips", "hip", "cog", "center"),
    "chest": ("chest", "spine2", "spine_02", "upperbody", "torso"),
    "neck": ("neck", "neck1"), "head": ("head", "head1"),
    "shoulder.l": ("leftshoulder", "shoulder_l", "lshoulder", "clavicle_l", "leftarm"),
    "upperarm.l": ("leftarm", "upperarm_l", "lupperarm", "arm_l"),
    "forearm.l": ("leftforearm", "lowerarm_l", "lforearm", "forearm_l"),
    "hand.l": ("lefthand", "hand_l", "lhand", "wrist_l"),
    "shoulder.r": ("rightshoulder", "shoulder_r", "rshoulder", "clavicle_r", "rightarm"),
    "upperarm.r": ("rightarm", "upperarm_r", "rupperarm", "arm_r"),
    "forearm.r": ("rightforearm", "lowerarm_r", "rforearm", "forearm_r"),
    "hand.r": ("righthand", "hand_r", "rhand", "wrist_r"),
    "hip.l": ("hip_l", "lhip", "lefthip"),
    "thigh.l": ("leftupleg", "upperleg_l", "lthigh", "thigh_l"),
    "shin.l": ("leftleg", "lowerleg_l", "lshin", "calf_l"),
    "ankle.l": ("leftankle", "ankle_l", "lankle"),
    "foot.l": ("leftfoot", "foot_l", "lfoot", "ball_l", "toe_l"),
    "hip.r": ("hip_r", "rhip", "righthip"),
    "thigh.r": ("rightupleg", "upperleg_r", "rthigh", "thigh_r"),
    "shin.r": ("rightleg", "lowerleg_r", "rshin", "calf_r"),
    "ankle.r": ("rightankle", "ankle_r", "rankle"),
    "foot.r": ("rightfoot", "foot_r", "rfoot", "ball_r", "toe_r"),
}

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

def auto_map_bones(project: StellarProject) -> list[str]:
    bones = project.source_manifest.get("bones", [])
    source_names = [str(b.get("name", "")) for b in bones if b.get("name")]
    normalized = {name: normalize_bone(name) for name in source_names}
    bone_parent = {str(b.get("name", "")): str(b.get("parent", "")) for b in bones}
    used: set[str] = set()
    report: list[str] = []
    by_id = {mapping.game_id: mapping for mapping in project.bone_mappings}
    # Deforming endpoints take precedence over virtual pivots. This prevents a
    # fuzzy `Root` match from consuming `RightFoot`, and lets rigs with three
    # leg bones map cleanly onto the game's five-node leg hierarchy.
    priority = [5, 6, 11, 12, 7, 8, 9, 10, 13, 14, 15, 16,
                19, 20, 22, 24, 25, 27, 4, 18, 21, 23, 26, 17, 28]
    for game_id in priority:
        mapping = by_id[game_id]
        if mapping.game_id < 4:
            mapping.source = ""
            mapping.confidence = 1.0
            continue
        if mapping.chain == "accessory":
            mapping.source = ""; mapping.confidence = 0.0
            continue
        target_key = mapping.target.casefold()
        aliases = SYNONYMS.get(target_key, (mapping.target,))
        alias_norms = [normalize_bone(v) for v in aliases]
        # The uppermost spine bone is normally the parent of shoulders/neck,
        # regardless of whether an exporter numbers its spine chain forwards
        # or backwards (Mia uses Spine02 -> Spine01 -> Spine).
        hierarchy_choice = ""
        if mapping.game_id == 6:
            shoulder_bones = [name for name in source_names if "shoulder" in normalized[name]]
            shoulder_parents = [bone_parent.get(name, "") for name in shoulder_bones]
            common = [name for name in shoulder_parents if name and shoulder_parents.count(name) >= 2]
            if common and common[0] not in used:
                hierarchy_choice = common[0]
        scored: list[tuple[float, str]] = []
        for name, norm in normalized.items():
            if name in used:
                continue
            score = max(difflib.SequenceMatcher(None, norm, alias).ratio() for alias in alias_norms)
            if norm in alias_norms:
                score = 1.0
            elif any(alias in norm or norm in alias for alias in alias_norms if len(alias) > 3):
                score = max(score, 0.88)
            side = ".l" if target_key.endswith(".l") else ".r" if target_key.endswith(".r") else ""
            if side:
                source_left = bool(re.search(r"(^|[^a-z])(l|left)([^a-z]|$)", name.casefold())) or "left" in norm
                source_right = bool(re.search(r"(^|[^a-z])(r|right)([^a-z]|$)", name.casefold())) or "right" in norm
                if (side == ".l" and source_right) or (side == ".r" and source_left):
                    score *= 0.25
            scored.append((score, name))
        if hierarchy_choice:
            scored.append((1.01, hierarchy_choice))
        if scored:
            score, name = max(scored)
            if score >= 0.72:
                mapping.source = name
                mapping.confidence = round(min(score, 1.0), 3)
                used.add(name)
                report.append(f"{mapping.game_id:02d} {mapping.target} <- {name} ({min(score, 1.0):.0%})")
            else:
                mapping.source = ""
                mapping.confidence = 0.0
    for mapping in project.bone_mappings:
        if mapping.game_id < 4:
            mapping.source = ""; mapping.confidence = 1.0
    # Game `.R` joints occupy +X while common FBX naming calls the anatomical
    # right side -X. Resolve that convention from bind coordinates after the
    # semantic match; otherwise a perfectly named Mixamo-style rig inherits
    # the opposite arm/leg rotations and looks twisted backwards.
    head_x = {str(b.get("name", "")): float((b.get("head") or [0.0])[0])
              for b in bones if b.get("name")}
    for right_id, left_id in ((7, 13), (8, 14), (9, 15), (10, 16),
                              (19, 24), (20, 25), (22, 27)):
        right = by_id[right_id]; left = by_id[left_id]
        if not right.source or not left.source:
            continue
        right_x = head_x.get(right.source, 0.0); left_x = head_x.get(left.source, 0.0)
        if right_x < left_x - 1.0e-6:
            right.source, left.source = left.source, right.source
            right.confidence, left.confidence = left.confidence, right.confidence
            report.append(f"{right_id:02d}/{left_id:02d} spatial side convention corrected (+X is game .R)")
    # Fill duplicate mechanical/helper targets from mapped ancestors only when
    # strict compatibility needs all game nodes; blank mappings remain visible.
    mapped_actions = {clip.source_action for clip in project.animations}
    for action in project.source_manifest.get("actions", []):
        name = str(action.get("name", "Action"))
        if name not in mapped_actions:
            start, end = action.get("frame_range", [0.0, 1.0])
            project.animations.append(AnimationClip(name=name, source_action=name,
                                                    game_action=name, start=start, end=end,
                                                    fps=float(action.get("fps", 30.0))))
    return report

def _blender_import_fbx(bpy: Any, path: Path, import_scale: float = 1.0) -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=str(path), use_anim=True,
                             global_scale=max(1.0e-6, float(import_scale)),
                             automatic_bone_orientation=False, ignore_leaf_bones=False)

def _prepare_fbx_export_textures(bpy: Any, meshes: list[Any], fbx_path: Path) -> tuple[
        list[tuple[Any, Any]], list[Any]]:
    """Swap oversized material images for capped copies during FBX export.

    Copies keep the imported scene and the saved editable `.blend` at source
    resolution.  The proxy bake also sees the originals after the swaps are
    restored, so this optimization is confined to `retargeted.fbx` and its
    generated texture payloads.
    """
    image_nodes: list[Any] = []
    seen_nodes: set[int] = set()
    for obj in meshes:
        for material in obj.data.materials:
            if material is None or material.node_tree is None:
                continue
            for node in material.node_tree.nodes:
                image = getattr(node, "image", None)
                if node.type != "TEX_IMAGE" or image is None or id(node) in seen_nodes:
                    continue
                seen_nodes.add(id(node))
                image_nodes.append(node)

    copies: dict[str, Any] = {}
    swaps: list[tuple[Any, Any]] = []
    generated: list[Any] = []
    texture_dir = fbx_path.parent / f"{fbx_path.stem}_textures"
    for node in image_nodes:
        source = node.image
        width, height = int(source.size[0]), int(source.size[1])
        target_width, target_height = fbx_texture_size(width, height)
        if (target_width, target_height) == (width, height):
            continue
        key = source.name_full
        resized = copies.get(key)
        if resized is None:
            texture_dir.mkdir(parents=True, exist_ok=True)
            resized = source.copy()
            resized.name = f"{source.name}_StellarFBX512"
            resized.scale(target_width, target_height)
            safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", Path(source.name).stem).strip("._") or "texture"
            texture_path = texture_dir / f"{len(copies):02d}_{safe_name}.png"
            resized.filepath_raw = str(texture_path)
            resized.file_format = "PNG"
            resized.save()
            # image.copy() also clones an embedded source FBX's packed bytes.
            # Those bytes are still the original (often 4K) PNG even after
            # scale()/save(), and Blender's FBX exporter prefers them over the
            # resized pixel buffer. Drop that inherited payload so embedding
            # reads the generated 512px PNG from filepath_raw instead.
            try:
                if resized.packed_file is not None:
                    resized.unpack(method="REMOVE")
            except RuntimeError:
                pass
            copies[key] = resized
            generated.append(resized)
            print(f"STELLAR: FBX texture {source.name_full} {width}x{height} -> "
                  f"{target_width}x{target_height}")
        swaps.append((node, source))
        node.image = resized
    return swaps, generated

def blender_worker(operation: str, input_path: Path, output_path: Path,
                   project_path: Optional[Path]) -> None:
    """Entry point executed inside Blender's bundled Python."""
    import bpy  # type: ignore
    worker_raw = json.loads(project_path.read_text(encoding="utf-8")) if project_path else {}

    if operation == "proxy":
        print(f"STELLAR: opening preserved retargeted scene {input_path}")
        bpy.ops.wm.open_mainfile(filepath=str(input_path))
    else:
        print(f"STELLAR: importing {input_path}")
        _blender_import_fbx(bpy, input_path, float(worker_raw.get("scale", 1.0)))
    armatures = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    armature = max(armatures, key=lambda obj: len(obj.data.bones), default=None)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    if operation == "inspect":
        bones: list[dict[str, Any]] = []
        if armature:
            for bone in armature.data.bones:
                bones.append({
                    "name": bone.name, "parent": bone.parent.name if bone.parent else "",
                    "head": [round(float(v), 7) for v in bone.head_local],
                    "tail": [round(float(v), 7) for v in bone.tail_local],
                    "length": round(float(bone.length), 7), "deform": bool(bone.use_deform),
                })
        mesh_data = []
        for obj in meshes:
            obj.data.calc_loop_triangles()
            weighted = sorted({group.name for group in obj.vertex_groups})
            mesh_data.append({
                "name": obj.name, "vertices": len(obj.data.vertices),
                "triangles": len(obj.data.loop_triangles), "materials": [m.name for m in obj.data.materials if m],
                "weighted_bones": weighted, "shape_keys": list(obj.data.shape_keys.key_blocks.keys()) if obj.data.shape_keys else [],
            })
        actions = []
        for action in bpy.data.actions:
            start, end = (float(v) for v in action.frame_range)
            preview_frames: list[dict[str, Any]] = []
            if armature:
                armature.animation_data_create()
                armature.animation_data.action = action
                sample_count = min(120, max(1, int(math.ceil(end - start)) + 1))
                sample_frames = [start] if sample_count == 1 else [start + (end - start) * i / (sample_count - 1) for i in range(sample_count)]
                for frame_value in sample_frames:
                    bpy.context.scene.frame_set(int(math.floor(frame_value)), subframe=frame_value % 1.0)
                    pose = {}
                    for pose_bone in armature.pose.bones:
                        point = (armature.matrix_world @ pose_bone.matrix).to_translation()
                        pose[pose_bone.name] = [round(float(v), 6) for v in point]
                    preview_frames.append({"frame": round(frame_value, 4), "bones": pose})
            actions.append({"name": action.name, "frame_range": [start, end],
                            "fcurves": len(action.fcurves), "fps": float(bpy.context.scene.render.fps),
                            "preview_frames": preview_frames})
        manifest = {
            "schema": "stellar.fbx-manifest.v2", "source": str(input_path),
            "blender": bpy.app.version_string, "armature": armature.name if armature else "",
            "bones": bones, "meshes": mesh_data, "actions": actions,
            "images": [{"name": image.name_full,
                        "size": [int(image.size[0]), int(image.size[1])]}
                       for image in bpy.data.images if int(image.size[0]) > 0 and int(image.size[1]) > 0],
            "scene_fps": float(bpy.context.scene.render.fps), "objects": len(bpy.context.scene.objects),
        }
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"STELLAR: inspected {len(bones)} bones, {len(mesh_data)} meshes, {len(actions)} actions")
        return
    if operation not in {"retarget", "proxy"}:
        raise ValueError(f"Unknown Blender worker operation: {operation}")
    if not armature:
        raise RuntimeError("The FBX contains no armature")
    if not project_path:
        raise RuntimeError("Retarget requires --project")
    raw = worker_raw
    mappings = [item for item in raw.get("bone_mappings", []) if item.get("source") and item.get("enabled", True)]
    old_to_new = {item["source"]: f"J{int(item['game_id']):02d}_{item['target'].replace('.', '_')}" for item in mappings}
    if operation == "proxy":
        target_names = {game_id: name for game_id, name, _parent, _chain in JOINT_SPECS}
        mappings = []
        old_to_new = {}
        for bone in armature.data.bones:
            match = re.match(r"J(\d+)_", bone.name)
            if match:
                game_id = int(match.group(1))
                mappings.append({"source": bone.name, "game_id": game_id,
                                 "target": target_names.get(game_id, bone.name), "enabled": True})
                old_to_new[bone.name] = bone.name
    # Bone and vertex-group renaming preserves geometry and weights.  Mapping
    # names become a stable ABI; non-game helper bones are retained as AUX so
    # artists can decide how to collapse them during rigid N64 mesh baking.
    for bone in list(armature.data.bones):
        old = bone.name
        if old in old_to_new:
            bone.name = old_to_new[old]
        elif not old.startswith("AUX_"):
            bone.name = "AUX_" + old
    for obj in meshes:
        for group in obj.vertex_groups:
            if group.name in old_to_new:
                group.name = old_to_new[group.name]
            # Blender normally renames the matching deform group when its bone
            # is renamed above. Do not turn that valid canonical group into an
            # AUX group during this second, defensive pass.
            elif re.match(r"J\d+_", group.name):
                pass
            elif not group.name.startswith("AUX_"):
                group.name = "AUX_" + group.name
    # Add unmapped engine/game placeholders without touching the mesh.  Their
    # parent chain makes the saved FBX structurally complete and editable.
    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    existing_ids = {int(item["game_id"]) for item in mappings}
    edit_by_id: dict[int, Any] = {}
    for item in mappings:
        bone = armature.data.edit_bones.get(old_to_new[item["source"]])
        if bone:
            edit_by_id[int(item["game_id"])] = bone
    for game_id, name, parent_id, _chain in JOINT_SPECS:
        if game_id in existing_ids:
            continue
        bone_name = f"J{game_id:02d}_{name.replace('.', '_')}"
        bone = armature.data.edit_bones.get(bone_name) or armature.data.edit_bones.new(bone_name)
        px, py, pz = REST_POSITIONS.get(game_id, (0.0, float(game_id), 0.0))
        scale = 0.01
        if parent_id in edit_by_id:
            parent = edit_by_id[parent_id]
            bone.parent = parent
            bone.head = parent.tail
        else:
            bone.head = (px * scale, py * scale, pz * scale)
        bone.tail = (bone.head.x, bone.head.y + max(0.01, 10.0 * scale), bone.head.z)
        bone.use_deform = False
        edit_by_id[game_id] = bone
    bpy.ops.object.mode_set(mode="OBJECT")
    armature["stellar_schema"] = "stellar.canonical-rig.v2"
    armature["stellar_base_character"] = raw.get("base_character", "Mario")
    armature["stellar_character"] = raw.get("name", "Fighter")
    if operation == "retarget":
        output_path.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(output_path))
        fbx_path = output_path.with_suffix(".fbx")
        bpy.ops.object.select_all(action="SELECT")
        texture_swaps, generated_images = _prepare_fbx_export_textures(bpy, meshes, fbx_path)
        try:
            bpy.ops.export_scene.fbx(filepath=str(fbx_path), use_selection=True, add_leaf_bones=False,
                                     bake_anim=True, path_mode="COPY", embed_textures=True,
                                     axis_forward=raw.get("axis_forward", "-Z"), axis_up=raw.get("axis_up", "Y"))
        finally:
            for node, source in texture_swaps:
                node.image = source
            for image in generated_images:
                bpy.data.images.remove(image)
    else:
        fbx_path = input_path.with_suffix(".fbx")
    # Build a separate N64/host proxy. The saved .blend/.fbx above keeps the
    # original topology and skin; only this proxy is decimated and rigidized.
    from mathutils import Euler, Matrix, Vector  # type: ignore

    base_character = str(raw.get("base_character", "Mario"))

    def group_game_joint(group_name: str) -> int:
        bone = armature.data.bones.get(group_name)
        while bone is not None:
            match = re.match(r"J(\d+)_", bone.name)
            if match:
                return runtime_joint_id(base_character, int(match.group(1)))
            bone = bone.parent
        return 5

    # Decimate first while Blender keeps interpolated vertex groups and the
    # original material slots.  Older Stellar builds tagged all ~300k source
    # faces in Python before reduction; that made a proxy bake take minutes.
    for obj in meshes:
        obj.data.calc_loop_triangles()
    source_triangles = sum(len(obj.data.loop_triangles) for obj in meshes)
    triangle_budget = max(500, int(raw.get("game_triangle_budget", 1200)))
    if source_triangles > triangle_budget:
        ratio = triangle_budget / source_triangles
        for obj in meshes:
            bpy.ops.object.select_all(action="DESELECT")
            obj.select_set(True); bpy.context.view_layer.objects.active = obj
            modifier = obj.modifiers.new(name="Stellar_GameBudget", type="DECIMATE")
            modifier.decimate_type = "COLLAPSE"; modifier.ratio = ratio
            modifier.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=modifier.name)

    target_world: dict[int, Any] = {}
    for entry in load_base_joint_tree(base_character):
        game_id = int(entry["game_id"])
        parent_id = int(entry["parent"])
        local = Matrix.Translation(Vector(entry["translation"]))
        local @= Euler(entry["rotation"], "XYZ").to_matrix().to_4x4()
        target_world[game_id] = target_world[parent_id] @ local if parent_id in target_world else local

    mapping_by_target = {item["target"]: old_to_new[item["source"]] for item in mappings}
    source_pelvis = armature.data.bones.get(mapping_by_target.get("Pelvis", ""))
    source_head = armature.data.bones.get(mapping_by_target.get("Head", ""))
    if source_pelvis is None or source_head is None:
        raise RuntimeError("Rigid game bake needs mapped Pelvis and Head bones")

    def blender_to_game(point: Any) -> Any:
        return Vector((float(point.x), float(point.z), -float(point.y)))

    source_pelvis_pos = blender_to_game(armature.matrix_world @ source_pelvis.head_local)
    source_head_pos = blender_to_game(armature.matrix_world @ source_head.head_local)
    target_pelvis_pos = target_world[5].translation.copy()
    target_head_pos = target_world[runtime_joint_id(base_character, 12)].translation.copy()
    source_height = max(0.001, (source_head_pos - source_pelvis_pos).length)
    target_height = max(1.0, (target_head_pos - target_pelvis_pos).length)
    skeleton_fit_scale = target_height / source_height
    visual_scale = float(raw.get("game_model_scale", 1.6))
    fit_scale = skeleton_fit_scale * visual_scale
    source_floor_y = min(
        float(blender_to_game(obj.matrix_world @ vertex.co).y)
        for obj in meshes for vertex in obj.data.vertices
    )
    target_floor_y = (float(target_pelvis_pos.y) +
                      (source_floor_y - float(source_pelvis_pos.y)) * skeleton_fit_scale)

    def normalize_source_point(point: Any) -> Any:
        return Vector((
            (float(point.x) - float(source_pelvis_pos.x)) * fit_scale + float(target_pelvis_pos.x),
            (float(point.y) - source_floor_y) * fit_scale + target_floor_y,
            (float(point.z) - float(source_pelvis_pos.z)) * fit_scale + float(target_pelvis_pos.z),
        ))
    basis_game = Matrix(((1.0, 0.0, 0.0, 0.0),
                         (0.0, 0.0, 1.0, 0.0),
                         (0.0, -1.0, 0.0, 0.0),
                         (0.0, 0.0, 0.0, 1.0)))
    def direction_frame(primary: Any, forward_hint: Any) -> Any:
        """Build a roll-stable bone frame from a segment direction.

        FBX bone roll is an authoring convention and often differs by 180
        degrees between otherwise identical rigs.  Treating that roll as a
        pose rotation flipped Mia's head and several limbs.  Segment direction
        plus a projected character-forward axis carries the useful orientation
        while discarding the arbitrary roll.
        """
        primary = primary.normalized()
        forward = forward_hint - primary * primary.dot(forward_hint)
        if forward.length < 1.0e-5:
            fallback = Vector((0.0, 1.0, 0.0))
            forward = fallback - primary * primary.dot(fallback)
        if forward.length < 1.0e-5:
            fallback = Vector((1.0, 0.0, 0.0))
            forward = fallback - primary * primary.dot(fallback)
        forward.normalize()
        right = primary.cross(forward).normalized()
        # Matrix() consumes rows; transpose to make these basis vectors columns.
        return Matrix((right, primary, forward)).transposed()

    source_frames: list[tuple[int, Any]] = []
    source_forward = Vector((0.0, 0.0, 1.0))
    for bone in armature.data.bones:
        match = re.match(r"J(\d+)_", bone.name)
        if match:
            head = blender_to_game(armature.matrix_world @ bone.head_local)
            tail = blender_to_game(armature.matrix_world @ bone.tail_local)
            direction = tail - head
            if direction.length < 1.0e-5:
                continue
            frame = direction_frame(direction, source_forward).to_4x4()
            frame.translation = normalize_source_point(head)
            source_frames.append((int(match.group(1)), frame))

    # If a compact base collapses semantic joints, retain the innermost source
    # frame deterministically instead of letting Blender's bone iteration order
    # replace Chest with Neck/Head at the same runtime DObj.
    source_frame_by_joint: dict[int, Any] = {}
    for semantic_joint, frame in sorted(source_frames, key=lambda item: item[0]):
        source_frame_by_joint.setdefault(runtime_joint_id(base_character, semantic_joint), frame)

    source_origin_by_joint = {game_id: frame.translation.copy()
                              for game_id, frame in source_frame_by_joint.items()}

    rigid_triangles: list[dict[str, Any]] = []
    image_cache: dict[str, Any] = {}
    preview_texture_path = output_path.parent / "source_texture.png"

    def material_vertex_color(material: Any, uv: Any, fallback: list[int]) -> list[int]:
        if material is None or material.node_tree is None:
            return fallback
        key = material.name_full
        if key not in image_cache:
            image = next((node.image for node in material.node_tree.nodes
                          if node.type == "TEX_IMAGE" and node.image is not None and node.image.size[0] > 0), None)
            if image is None:
                image_cache[key] = None
            else:
                # A Blender RNA lookup per channel/per vertex makes even a 3k
                # pass-through mesh take minutes.  One bulk slice keeps the
                # same texture samples and reduces the proxy pass to seconds.
                image_cache[key] = (int(image.size[0]), int(image.size[1]), list(image.pixels[:]), image.name_full)
                if not preview_texture_path.is_file():
                    try:
                        # Keep the embedded FBX atlas at its original
                        # resolution for Stellar. The N64 atlas below is a
                        # separate runtime fallback and must never become the
                        # editor's source of truth.
                        image.save_render(filepath=str(preview_texture_path))
                    except Exception as exc:
                        print(f"STELLAR: full-resolution texture save skipped: {exc}")
        cached = image_cache[key]
        if cached is None:
            return fallback
        try:
            width, height, pixels, _image_name = cached
            x = min(width - 1, max(0, int((float(uv[0]) % 1.0) * width)))
            y = min(height - 1, max(0, int((float(uv[1]) % 1.0) * height)))
            offset = (y * width + x) * 4
            # Blender exposes image samples in scene-linear RGB. Store sRGB-ish
            # vertex colors so the untextured host proxy resembles the source.
            rgb = [max(0, min(255, round((max(0.0, float(pixels[offset + i])) ** (1.0 / 2.2)) * 255.0))) for i in range(3)]
            # The host proxy uses opaque vertex shading. FBX exporters often
            # leave the diffuse image alpha at zero even when the material is
            # visually opaque, which previously produced a white/cutout Mia.
            return rgb + [255]
        except Exception:
            return fallback

    for obj in meshes:
        obj.data.calc_loop_triangles()
        normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
        uv_layer = obj.data.uv_layers.active.data if obj.data.uv_layers.active else None
        for triangle in obj.data.loop_triangles:
            material = obj.data.materials[triangle.material_index] if triangle.material_index < len(obj.data.materials) else None
            triangle_weights: dict[int, float] = {}
            for vertex_index in triangle.vertices:
                for assignment in obj.data.vertices[vertex_index].groups:
                    influence_joint = group_game_joint(obj.vertex_groups[assignment.group].name)
                    triangle_weights[influence_joint] = triangle_weights.get(influence_joint, 0.0) + float(assignment.weight)
            joint_id = max(triangle_weights, key=triangle_weights.get, default=5)
            if joint_id not in target_world:
                joint_id = 5
            diffuse = material.diffuse_color if material is not None else (0.72, 0.72, 0.76, 1.0)
            fallback_color = [max(0, min(255, round(float(diffuse[index]) * 255.0))) for index in range(3)] + [255]
            inverse_target = target_world[joint_id].inverted()
            vertices = []
            for vertex_index, loop_index in zip(triangle.vertices, triangle.loops):
                vertex = obj.data.vertices[vertex_index]
                source_world = obj.matrix_world @ vertex.co
                weights: dict[int, float] = {}
                for assignment in vertex.groups:
                    group_name = obj.vertex_groups[assignment.group].name
                    influence_joint = group_game_joint(group_name)
                    if influence_joint in target_world:
                        weights[influence_joint] = weights.get(influence_joint, 0.0) + float(assignment.weight)
                ranked = sorted(weights.items(), key=lambda item: item[1], reverse=True)[:2]
                total_weight = sum(weight for _influence_joint, weight in ranked) or 1.0
                normalized_game = normalize_source_point(blender_to_game(source_world))
                # The bind mesh must remain byte-for-byte in its normalized
                # source shape.  Animation later applies the inherited target
                # *delta* around these source pivots; fitting every section to
                # Mario's absolute pivots collapses rigs whose spine/shoulder
                # layout has different zero-length helper nodes.
                game_world = normalized_game
                local = inverse_target @ game_world
                normal = blender_to_game(normal_matrix @ vertex.normal).normalized()
                uv = uv_layer[loop_index].uv if uv_layer else (0.0, 0.0)
                skin = []
                for influence_joint, weight in ranked or [(joint_id, 1.0)]:
                    source_origin = source_origin_by_joint.get(influence_joint, target_world[influence_joint].translation)
                    bind_relative = target_world[influence_joint].to_3x3().transposed() @ (game_world - source_origin)
                    skin.append({
                        "joint": influence_joint, "weight": round(float(weight) / total_weight, 6),
                        "position": [round(float(value), 4) for value in bind_relative],
                    })
                vertices.append({
                    "position": [round(float(v), 4) for v in local],
                    "normal": [round(float(v), 5) for v in normal],
                    "uv": [round(float(uv[0]), 6), round(float(uv[1]), 6)],
                    "color": material_vertex_color(material, uv, fallback_color),
                    "skin": skin,
                })
            rigid_triangles.append({"joint": joint_id, "vertices": vertices})
    proxy_texture = None
    source_image = next((cached for cached in image_cache.values() if cached is not None), None)
    if source_image is not None:
        try:
            texture_size = 512
            source_width, source_height, source_pixels, source_image_name = source_image
            samples: list[tuple[int, int, int, int]] = []
            for y in range(texture_size):
                source_y = min(source_height - 1, round(y * (source_height - 1) / (texture_size - 1)))
                for x in range(texture_size):
                    source_x = min(source_width - 1, round(x * (source_width - 1) / (texture_size - 1)))
                    offset = (source_y * source_width + source_x) * 4
                    channels = [max(0, min(255, round((max(0.0, float(source_pixels[offset + i])) ** (1.0 / 2.2)) * 255.0))) for i in range(3)]
                    alpha = max(0, min(255, round(float(source_pixels[offset + 3]) * 255.0)))
                    samples.append((channels[0], channels[1], channels[2], alpha))
            # A direct 40x40 RGBA16 atlas occupies 3200 bytes and therefore
            # fits in the RDP's 4 KiB TMEM without a palette.  Avoiding CI4 is
            # important here: palette/TLUT state leaked between the fighter
            # and CSS passes in the host renderer and produced blue/green
            # triangles even though the UVs and source texture were valid.
            rgba16 = [((red >> 3) << 11) | ((green >> 3) << 6) |
                      ((blue >> 3) << 1) | int(alpha >= 32)
                      for red, green, blue, alpha in samples]
            proxy_texture = {"width": texture_size, "height": texture_size,
                             "format": "rgba16", "pixels": rgba16,
                             "source": source_image_name,
                             "preview_file": preview_texture_path.name if preview_texture_path.is_file() else ""}
        except Exception as exc:
            print(f"STELLAR: proxy texture sampling skipped: {exc}")
    rigid_path = output_path.parent / "rigid_mesh.json"
    rigid_payload = {
        "schema": "stellar.skinned-game-mesh.v2", "source_triangles": source_triangles,
        "triangles": len(rigid_triangles), "triangle_budget": triangle_budget,
        "fit_scale": fit_scale, "base_skeleton": raw.get("base_character", "Mario"),
        "joint_space": "base-runtime",
        "runtime_joint_layout": RUNTIME_JOINT_LAYOUT_VERSION,
        "baked_model_scale": float(raw.get("game_model_scale", 1.6)),
        "skinning": {"method": "source-pivot inherited-pose delta", "influences": 2},
        "bind_joints": {str(game_id): [round(float(value), 4) for value in origin]
                        for game_id, origin in source_origin_by_joint.items()},
        "texture": proxy_texture,
        "geometry": rigid_triangles,
    }
    normalize_rigid_skin_scale(rigid_payload)
    rigid_path.write_text(json.dumps(rigid_payload, separators=(",", ":")) + "\n", encoding="utf-8")
    summary = {
        "schema": "stellar.mesh-summary.v1", "armature": armature.name,
        "canonical_bones": len(edit_by_id), "auxiliary_bones": sum(b.name.startswith("AUX_") for b in armature.data.bones),
        "meshes": [{"name": obj.name, "vertices": len(obj.data.vertices),
                    "triangles": len(obj.data.loop_triangles)} for obj in meshes],
        "geometry_preserved": True,
        "game_proxy_triangles": len(rigid_triangles), "game_proxy": rigid_path.name,
        "n64_note": "Bake weighted triangles to dominant rigid game joints and reduce to an N64-suitable polygon/material budget.",
    }
    (output_path.parent / "mesh_summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"STELLAR: saved weighted proxy {rigid_path}")

GAME_DIR = Path(__file__).resolve().parents[3] / 'ssb-decomp-re'

if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--') + 1:]
    blender_worker(args[0], Path(args[1]), Path(args[2]), Path(args[3]) if len(args)>3 else None)
