from __future__ import annotations

import csv
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .model import Camera3D, FighterWrapper, GeometryLayout, Model3D, ModelLoader
from .types import Color, Vec3


@dataclass
class ResourceDesc:
    bundle: str
    key: str
    kind: str
    descriptor: str
    animation: str
    layout: GeometryLayout
    materials: str
    material_animation: str
    animation_file: int
    wrapper: FighterWrapper
    dependency: str
    transition_frame: float
    material_start: float
    lighting: str
    position: Vec3
    setup_parts: tuple[int, int]


@dataclass
class RenderCue:
    resource: str
    camera: str
    order: int
    tint: Color


@dataclass
class TimelineSegment:
    name: str
    duration: int
    renderer: str
    argument: str
    bundle: str
    preload_lead: int
    scale: tuple[float, float]
    color: Color
    cues: list[RenderCue] = field(default_factory=list)


@dataclass
class TimelinePosition:
    segment: TimelineSegment
    index: int
    local: int
    start: int


FIGHTER_DESCRIPTORS = {
    "Mario": ("llMarioModelJointTreeDObjDesc", 362),
    "Donkey": ("llDonkeyModelJointTreeDObjDesc", 0),
    "Link": ("llLinkModelJointTreeDObjDesc", 409),
    "Samus": ("llSamusModelJointTreeDObjDesc", 0),
    "Yoshi": ("llYoshiModelJointTreeDObjDesc", 0),
    "Kirby": ("llKirbyModelJointTreeDObjDesc", 0),
    "Fox": ("llFoxModelJointTreeDObjDesc", 0),
    "Pikachu": ("llPikachuModelJointTreeDObjDesc", 0),
}


def _blank(value: str) -> str:
    return "" if value in ("", "-") else value


def _layout(value: str) -> GeometryLayout:
    return {"direct": GeometryLayout.Direct, "links": GeometryLayout.DisplayListLinks,
            "pairs": GeometryLayout.JointPairs}[value]


def _wrapper(value: str) -> FighterWrapper:
    return {"none": FighterWrapper.None_, "transn": FighterWrapper.TransN,
            "xrotn": FighterWrapper.XRotN}[value]


class SceneTable:
    """Editable opening animation graph (the TSV sources OpeningScene.cpp plays)."""

    def __init__(self, scene: Path, sequence: Path, cues: Path) -> None:
        self.resources: list[ResourceDesc] = []
        self.bundles: dict[str, list[ResourceDesc]] = {}
        self.by_key: dict[str, ResourceDesc] = {}
        self.segments: list[TimelineSegment] = []
        self._load_resources(scene)
        self._load_sequence(sequence)
        self._load_cues(cues)
        self.total_duration = sum(segment.duration for segment in self.segments)

    def _load_resources(self, path: Path) -> None:
        with path.open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                resource = ResourceDesc(
                    bundle=row["bundle"],
                    key=row["key"],
                    kind=row["kind"],
                    descriptor=_blank(row["descriptor"]),
                    animation=_blank(row["animation"]),
                    layout=_layout(row["layout"]),
                    materials=_blank(row["materials"]),
                    material_animation=_blank(row["material_animation"]),
                    animation_file=int(row["animation_file"]),
                    wrapper=_wrapper(row["wrapper"]),
                    dependency=_blank(row["dependency"]),
                    transition_frame=float(row["transition_frame"]),
                    material_start=float(row["material_start"]),
                    lighting=row["lighting"],
                    position=Vec3(float(row["position_x"]), float(row["position_y"]),
                                  float(row["position_z"])),
                    setup_parts=(int(row.get("setup_flags0") or "0xffffffff", 0),
                                 int(row.get("setup_flags1") or "0xffffffff", 0)),
                )
                self.resources.append(resource)
                self.bundles.setdefault(resource.bundle, []).append(resource)
                self.by_key[resource.key] = resource

    def _load_sequence(self, path: Path) -> None:
        with path.open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                self.segments.append(TimelineSegment(
                    name=row["name"],
                    duration=int(row["duration"]),
                    renderer=row["renderer"],
                    argument=_blank(row["argument"]),
                    bundle=_blank(row["bundle"]),
                    preload_lead=int(row["preload_lead"]),
                    scale=(float(row["scale_x"]), float(row["scale_y"])),
                    color=Color(int(row["red"]), int(row["green"]), int(row["blue"]), int(row["alpha"])),
                ))

    def _load_cues(self, path: Path) -> None:
        names = {segment.name: segment for segment in self.segments}
        with path.open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                names[row["segment"]].cues.append(RenderCue(
                    resource=row["resource"],
                    camera=_blank(row["camera"]),
                    order=int(row["order"]),
                    tint=Color(int(row["red"]), int(row["green"]), int(row["blue"]), int(row["alpha"])),
                ))
        for segment in self.segments:
            segment.cues.sort(key=lambda cue: cue.order)

    def locate(self, tic: int) -> TimelinePosition:
        start = 0
        for index, segment in enumerate(self.segments):
            if tic < start + segment.duration:
                return TimelinePosition(segment, index, tic - start, start)
            start += segment.duration
        last = self.segments[-1]
        return TimelinePosition(last, len(self.segments) - 1, last.duration - 1,
                                self.total_duration - last.duration)

    def build_resource(self, loader: ModelLoader, resource: ResourceDesc,
                       available: Optional[dict[str, Model3D]] = None) -> Model3D:
        available = available or {}
        if resource.kind == "model":
            model = loader.model(resource.descriptor, resource.animation, resource.layout,
                                 resource.materials, resource.material_animation)
        elif resource.kind == "display":
            model = loader.display_list(resource.descriptor, resource.layout,
                                        resource.materials, resource.material_animation)
            if resource.animation:
                symbol = loader.archive.require_symbol(resource.animation.lstrip("@"))
                model.animation[0] = loader.archive.resolve(symbol) if resource.animation.startswith("@") else symbol
        elif resource.kind == "fighter":
            model = loader.fighter_model(resource.descriptor, resource.layout, resource.setup_parts)
            loader.bind_fighter_animation(model, resource.animation_file, resource.wrapper)
        elif resource.kind == "transition":
            previous = available.get(resource.dependency)
            if previous is None:
                raise RuntimeError(f"scene transition dependency is not loaded: {resource.dependency}")
            model = loader.transition(previous, resource.animation_file, resource.wrapper,
                                      resource.transition_frame)
        else:
            raise RuntimeError(f"unknown resource kind: {resource.kind}")
        model.key = resource.key
        model.receive_lighting = resource.lighting not in ("unlit", "spot")
        model.emit_spotlight = resource.lighting == "spot"
        model.additive = resource.lighting == "spot"
        model.material_animation_start = resource.material_start
        model.position = resource.position
        return model

    def build_bundle(self, loader: ModelLoader, name: str,
                     available: Optional[dict[str, Model3D]] = None) -> dict[str, Model3D]:
        models = dict(available or {})
        built: dict[str, Model3D] = {}
        for resource in self.bundles[name]:
            model = self.build_resource(loader, resource, models)
            models[resource.key] = model
            built[resource.key] = model
        return built


def opening_room_camera(loader: ModelLoader, local: int) -> Camera3D:
    if local < 560:
        camera = Camera3D(near_plane=80, far_plane=15000)
        return loader.camera("llMVOpeningRoomScene1CamAnimJoint", float(local), camera)
    if local < 860:
        return loader.camera("llMVOpeningRoomScene2CamAnimJoint", float(local - 560))
    if local < 1140:
        camera = Camera3D(eye=Vec3(9.2993, 3880.3894, 4077.9817),
                          at=Vec3(0.991579, 2995.6814, -388.95343),
                          fov_y=18.607187, near_plane=128, far_plane=16384)
        return loader.camera("llMVOpeningRoomScene3CamAnimJoint", float(local - 860), camera)
    camera = Camera3D(eye=Vec3(-1039.8806, 3199.2156, -1235.1688),
                      at=Vec3(-1162.4098, 2127.8245, -3853.0732),
                      fov_y=11.982265, near_plane=128, far_plane=16384)
    return loader.camera("llMVOpeningRoomScene4CamAnimJoint", float(local - 1140), camera)


def fighter_from_portrait(argument: str) -> Optional[tuple[str, int]]:
    for name, spec in FIGHTER_DESCRIPTORS.items():
        if name in argument:
            return spec
    return None
