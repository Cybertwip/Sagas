"""Editable Stellar project schema and JSON persistence lifecycle."""

from __future__ import annotations

import copy
import dataclasses
from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Any, Callable, Optional
import uuid

from stellar_shared import atomic_json, clean_symbol, now_iso


_PROJECT_VERSION = 3
_APP_VERSION = "0.9.1"
_JOINT_SPECS: list[tuple[int, str, int, str]] = []
_DEFAULT_STATS: dict[str, float] = {}
_LOAD_BASE_STATS: Optional[Callable[[str], dict[str, float]]] = None
_LOAD_BASE_HITBOXES: Optional[Callable[[str], list["Hitbox"]]] = None


def configure_project_schema(*, project_version: int, app_version: str,
                             joint_specs: list[tuple[int, str, int, str]],
                             default_stats: dict[str, float],
                             load_base_stats: Callable[[str], dict[str, float]],
                             load_base_hitboxes: Callable[[str], list["Hitbox"]]) -> None:
    """Bind repository-specific defaults without coupling the schema to the CLI."""
    global _PROJECT_VERSION, _APP_VERSION, _JOINT_SPECS, _DEFAULT_STATS
    global _LOAD_BASE_STATS, _LOAD_BASE_HITBOXES
    _PROJECT_VERSION = project_version
    _APP_VERSION = app_version
    _JOINT_SPECS = joint_specs
    _DEFAULT_STATS = default_stats
    _LOAD_BASE_STATS = load_base_stats
    _LOAD_BASE_HITBOXES = load_base_hitboxes


@dataclass
class BoneMapping:
    game_id: int
    target: str
    parent_id: int
    chain: str
    source: str = ""
    enabled: bool = True
    confidence: float = 0.0
    rotation_offset: list[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    translation_scale: float = 1.0


@dataclass
class AnimationClip:
    name: str
    source_action: str = ""
    game_action: str = "Wait"
    start: float = 0.0
    end: float = 1.0
    fps: float = 30.0
    loop: bool = True
    root_motion: str = "extract"
    enabled: bool = True
    controller_sequence: str = ""
    source_kind: str = "custom"
    source_path: str = ""
    inherited: bool = False


@dataclass
class Hitbox:
    uid: str = field(default_factory=lambda: uuid.uuid4().hex[:10])
    move: str = "Attack11"
    attack_id: int = 0
    group_id: int = 0
    joint_id: int = 10
    start_frame: float = 2.0
    end_frame: float = 4.0
    damage: int = 6
    size: int = 200
    offset: list[int] = field(default_factory=lambda: [0, 100, 0])
    angle: int = 361
    knockback_scale: int = 100
    knockback_weight: int = 0
    knockback_base: int = 0
    shield_damage: int = 0
    element: int = 0
    sound_level: int = 1
    sound_kind: int = 1
    ground_air: int = 3
    rebound: bool = True
    scaled: bool = False
    enabled: bool = True

    def __post_init__(self) -> None:
        if 512 <= self.angle <= 1023:
            self.angle -= 1024


@dataclass
class SoundAsset:
    name: str = "Announcer"
    path: str = ""
    kind: str = "voice"
    event: str = "Select"
    pitch_semitones: float = 0.0
    gain_db: float = 0.0
    trim_start: float = 0.0
    trim_end: float = 0.0
    loop: bool = False
    game_voice_id: str = ""
    inherited: bool = False
    source_kind: str = "custom"
    fgm_id: int = -1
    wave_paths: list[str] = field(default_factory=list)
    wave_pitch_cents: list[int] = field(default_factory=list)


@dataclass
class StageAsset:
    name: str = "New Stage"
    model_path: str = ""
    collision_path: str = ""
    base_stage: str = "Dream Land"
    blast_bounds: list[float] = field(default_factory=lambda: [-4500.0, 4500.0, 3500.0, -2500.0])
    camera_bounds: list[float] = field(default_factory=lambda: [-3500.0, 3500.0, 2500.0, -1500.0])
    enabled: bool = True


def _mappings() -> list[BoneMapping]:
    return [BoneMapping(game_id, target, parent_id, chain)
            for game_id, target, parent_id, chain in _JOINT_SPECS]


@dataclass
class StellarProject:
    project_version: int = 3
    app_version: str = "0.9.1"
    name: str = "Mia"
    symbol: str = "Mia"
    base_character: str = "Mario"
    model_path: str = "Mia.fbx"
    portrait_path: str = "Mia.png"
    author: str = ""
    description: str = ""
    scale: float = 1.0
    axis_forward: str = "-Z"
    axis_up: str = "Y"
    strict_skeleton: bool = True
    game_triangle_budget: int = 1200
    game_model_scale: float = 1.6
    global_pitch_semitones: float = 0.0
    inherited_pitch_overrides: dict[str, float] = field(default_factory=dict)
    bone_mappings: list[BoneMapping] = field(default_factory=list)
    source_manifest: dict[str, Any] = field(default_factory=dict)
    animations: list[AnimationClip] = field(default_factory=list)
    hitboxes: list[Hitbox] = field(default_factory=list)
    stats: dict[str, float] = field(default_factory=lambda: copy.deepcopy(_DEFAULT_STATS))
    sounds: list[SoundAsset] = field(default_factory=list)
    stages: list[StageAsset] = field(default_factory=list)
    input_bindings: dict[str, str] = field(default_factory=dict)
    notes: str = ""
    created: str = field(default_factory=now_iso)
    modified: str = field(default_factory=now_iso)

    @classmethod
    def new(cls, character_dir: Path, name: Optional[str] = None, base: str = "Mario") -> "StellarProject":
        char_name = name or character_dir.name or "New Fighter"
        project = cls(name=char_name, symbol=clean_symbol(char_name), base_character=base)
        fbxs = sorted(character_dir.glob("*.fbx"))
        portraits = sorted(character_dir.glob("*.png"))
        wavs = sorted(character_dir.glob("*.wav"))
        project.model_path = fbxs[0].name if fbxs else ""
        project.portrait_path = portraits[0].name if portraits else ""
        project.bone_mappings = _mappings()
        if wavs:
            project.sounds = [SoundAsset(path=wavs[0].name, kind="announcer")]
        project.stats = _LOAD_BASE_STATS(base) if _LOAD_BASE_STATS else copy.deepcopy(_DEFAULT_STATS)
        project.hitboxes = _LOAD_BASE_HITBOXES(base) if _LOAD_BASE_HITBOXES else []
        project.input_bindings = {
            "Attack11": "A", "AttackDash": "Stick + A", "AttackS3": "Tilt + A",
            "AttackS4": "Smash + A", "AttackAirN": "Air + A", "SpecialN": "B",
            "SpecialHi": "Up + B", "SpecialLw": "Down + B", "Catch": "Z",
        }
        return project

    @classmethod
    def load(cls, path: Path) -> "StellarProject":
        raw = json.loads(path.read_text(encoding="utf-8"))
        known = {item.name for item in dataclasses.fields(cls)}
        values = {key: value for key, value in raw.items() if key in known}
        values["bone_mappings"] = [BoneMapping(**value) for value in raw.get("bone_mappings", [])]
        values["animations"] = [AnimationClip(**value) for value in raw.get("animations", [])]
        values["hitboxes"] = [Hitbox(**value) for value in raw.get("hitboxes", [])]
        values["sounds"] = [SoundAsset(**value) for value in raw.get("sounds", [])]
        values["stages"] = [StageAsset(**value) for value in raw.get("stages", [])]
        project = cls(**values)
        project.project_version = _PROJECT_VERSION
        if not project.bone_mappings:
            project.bone_mappings = _mappings()
        merged_stats = copy.deepcopy(_DEFAULT_STATS)
        merged_stats.update(project.stats)
        project.stats = merged_stats
        return project

    def save(self, path: Path) -> None:
        self.modified = now_iso()
        self.app_version = _APP_VERSION
        self.project_version = _PROJECT_VERSION
        atomic_json(path, dataclasses.asdict(self))
