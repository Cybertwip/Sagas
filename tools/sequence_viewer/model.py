from __future__ import annotations

import copy
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

from .animation import (
    JointPose, MaterialPose, animation_table, apply_pose, pose_from_node,
    sample16, sample32, sample_material,
)
from .archive import RelocArchive
from .display_list import DisplayListDecoder
from .math3d import Matrix, identity, multiply, rotation, scale, translation
from .skeleton import decode_skeleton
from .types import Address, Color, Material, Mesh, Node, Vec3


class GeometryLayout(Enum):
    Direct = "direct"
    DisplayListLinks = "links"
    JointPairs = "pairs"


class FighterWrapper(Enum):
    None_ = "none"
    TransN = "transn"
    XRotN = "xrotn"


FIGHTER_SETUP_PARTS: dict[str, tuple[int, int]] = {
    "llLuigiModelJointTreeDObjDesc": (0xffffff00, 0),
    "llMarioModelJointTreeDObjDesc": (0xffffff00, 0),
    "llDonkeyModelJointTreeDObjDesc": (0xffffff80, 0),
    "llLinkModelJointTreeDObjDesc": (0xfff9fffe, 0),
    "llSamusModelJointTreeDObjDesc": (0xfff803ff, 0),
    "llCaptainModelJointTreeDObjDesc": (0xffffff80, 0),
    "llNessModelJointTreeDObjDesc": (0xffffffc0, 0),
    "llYoshiModelJointTreeDObjDesc": (0xfbffffe0, 0),
    "llKirbyModelJointTreeDObjDesc": (0xef7cffc0, 0),
    "llFoxModelJointTreeDObjDesc": (0xffffffc0, 0),
    "llPikachuModelJointTreeDObjDesc": (0xffffffc0, 0),
    "llPurinModelJointTreeDObjDesc": (0xeff9ff80, 0),
    "llBossModelJointTreeDObjDesc": (0xffffff80, 0),
}

FIGHTER_LAYOUTS: dict[str, GeometryLayout] = {
    "llYoshiModelJointTreeDObjDesc": GeometryLayout.JointPairs,
    "llBossModelJointTreeDObjDesc": GeometryLayout.JointPairs,
}


@dataclass
class Camera3D:
    eye: Vec3 = field(default_factory=lambda: Vec3(0, 0, 1000))
    at: Vec3 = field(default_factory=Vec3)
    up: Vec3 = field(default_factory=lambda: Vec3(0, 1, 0))
    fov_y: float = 45.0
    near_plane: float = 16.0
    far_plane: float = 65536.0


@dataclass
class Model3D:
    key: str = ""
    nodes: list[Node] = field(default_factory=list)
    meshes: list[Mesh] = field(default_factory=list)
    parent_meshes: list[Mesh] = field(default_factory=list)
    materials: list[list[Material]] = field(default_factory=list)
    material_animation: list[list[Optional[Address]]] = field(default_factory=list)
    animation: list[Optional[Address]] = field(default_factory=list)
    fighter_root: Node = field(default_factory=Node)
    fighter_root_animation: Optional[Address] = None
    fighter_wrapper: FighterWrapper = FighterWrapper.None_
    fighter_animation: bool = False
    is_fighter: bool = False
    receive_lighting: bool = True
    emit_spotlight: bool = False
    additive: bool = False
    material_animation_start: float = 0.0
    position: Vec3 = field(default_factory=Vec3)
    rotation: Vec3 = field(default_factory=Vec3)
    scale: Vec3 = field(default_factory=lambda: Vec3(1, 1, 1))
    layout: GeometryLayout = GeometryLayout.Direct
    descriptor: str = ""
    animation_symbol: str = ""
    material_symbol: str = ""
    material_animation_symbol: str = ""


def _material_animation_table(
    archive: RelocArchive, table: Address, materials: list[list[Material]],
) -> list[list[Optional[Address]]]:
    result: list[list[Optional[Address]]] = [[] for _ in materials]
    for node, node_materials in enumerate(materials):
        result[node] = [None] * len(node_materials)
        node_slot = table.shifted(node * 4)
        if archive.u32(node_slot) == 0:
            continue
        scripts = archive.resolve(node_slot)
        if scripts is None:
            continue
        for material in range(len(node_materials)):
            script_slot = scripts.shifted(material * 4)
            if archive.u32(script_slot) != 0:
                result[node][material] = archive.resolve(script_slot)
    return result


class ModelLoader:
    def __init__(self, archive: RelocArchive) -> None:
        self.archive = archive
        self.decoder = DisplayListDecoder(archive)

    def model(
        self,
        descriptor: str,
        animation: str = "",
        layout: GeometryLayout = GeometryLayout.DisplayListLinks,
        materials: str = "",
        material_animation: str = "",
    ) -> Model3D:
        desc = self.archive.require_symbol(descriptor)
        model = Model3D(descriptor=descriptor, animation_symbol=animation,
                        material_symbol=materials, material_animation_symbol=material_animation,
                        layout=layout)
        model.nodes = decode_skeleton(self.archive, desc)
        model.meshes = [Mesh() for _ in model.nodes]
        model.parent_meshes = [Mesh() for _ in model.nodes]
        node_materials: list[list[Material]] = [[] for _ in model.nodes]
        if materials:
            table = self.archive.require_symbol(materials)
            node_materials = self.decoder.materials(table, len(model.nodes))
        model.materials = node_materials
        model.material_animation = [[] for _ in model.nodes]
        if material_animation:
            table = self.archive.require_symbol(material_animation)
            model.material_animation = _material_animation_table(self.archive, table, model.materials)
        self._decode_geometry(model, layout)
        if animation:
            symbol = self.archive.require_symbol(animation)
            model.animation = animation_table(self.archive, symbol, len(model.nodes))
        else:
            model.animation = [None] * len(model.nodes)
        return model

    def fighter_model(
        self,
        descriptor: str,
        layout: GeometryLayout = GeometryLayout.Direct,
        setup_parts: Optional[tuple[int, int]] = None,
    ) -> Model3D:
        desc = self.archive.require_symbol(descriptor)
        setup_parts = setup_parts or FIGHTER_SETUP_PARTS.get(
            descriptor, (0xffffffff, 0xffffffff))
        model = Model3D(descriptor=descriptor, layout=layout, is_fighter=True)
        source_nodes = decode_skeleton(self.archive, desc)
        source_materials = self.decoder.materials(Address(desc.file, 0), len(source_nodes))

        # Same FTData attribute offsets as the C++ fighter loader.
        attributes = {
            296: (203, 0x428), 313: (209, 0x46c), 317: (213, 0x4a4),
            320: (217, 0x610), 323: (221, 0x580), 324: (225, 0x708),
            338: (247, 0x47c), 332: (236, 0x488), 328: (229, 0x808),
            341: (243, 0x41c), 330: (233, 0x474), 335: (239, 0x5bc),
        }
        if desc.file in attributes:
            attr = Address(*attributes[desc.file])
            parts = self.archive.resolve(attr.shifted(0x2d4))
            costumes = self.archive.resolve(parts.shifted(8)) if parts else None
            if costumes:
                scripts = _material_animation_table(self.archive, costumes, source_materials)
                for materials, joint_scripts in zip(source_materials, scripts):
                    for material, script in zip(materials, joint_scripts):
                        if script is None:
                            continue
                        initial = MaterialPose()
                        initial.colors[0] = material.primitive
                        if material.light1:
                            initial.colors[3] = material.light1
                        if material.light2:
                            initial.colors[4] = material.light2
                        pose = sample_material(self.archive, script, 0, initial)
                        material.primitive = pose.colors[0]
                        if material.light1:
                            material.light1 = pose.colors[3]
                        if material.light2:
                            material.light2 = pose.colors[4]
                        if material.sprites:
                            material.image = self.archive.resolve(material.sprites.shifted(4 * int(pose.tracks[0])))
                        if material.palettes:
                            material.palette = self.archive.resolve(material.palettes.shifted(4 * int(pose.tracks[9])))

        def enabled(index: int) -> bool:
            word = index // 32
            return word < len(setup_parts) and bool(setup_parts[word] & (1 << (31 - index % 32)))

        model.nodes = [node for index, node in enumerate(source_nodes) if enabled(index)]
        model.materials = [materials for index, materials in enumerate(source_materials) if enabled(index)]
        model.meshes = [Mesh() for _ in model.nodes]
        model.parent_meshes = [Mesh() for _ in model.nodes]
        model.animation = [None] * len(model.nodes)
        model.material_animation = [[] for _ in model.nodes]
        self._decode_geometry(model, layout)
        return model

    def display_list(
        self,
        symbol: str,
        layout: GeometryLayout = GeometryLayout.Direct,
        materials: str = "",
        material_animation: str = "",
    ) -> Model3D:
        address = self.archive.require_symbol(symbol)
        model = Model3D(descriptor=symbol, layout=layout, material_symbol=materials,
                        material_animation_symbol=material_animation)
        model.nodes = [Node(display_list=address)]
        model.parent_meshes = [Mesh()]
        node_materials: list[Material] = []
        if materials:
            table = self.archive.require_symbol(materials)
            node_materials = self.decoder.materials(table, 1)[0]
        model.materials = [node_materials]
        model.material_animation = [[]]
        if material_animation:
            table = self.archive.require_symbol(material_animation)
            model.material_animation = _material_animation_table(self.archive, table, model.materials)
        if layout == GeometryLayout.JointPairs:
            parent = self.archive.resolve(address)
            local = self.archive.resolve(address.shifted(4))
            if parent:
                model.parent_meshes[0] = self.decoder.decode(parent, node_materials)
            model.meshes = [self.decoder.decode(local, node_materials) if local else Mesh()]
        elif layout == GeometryLayout.DisplayListLinks:
            model.meshes = [self.decoder.decode_links(address, node_materials)]
        else:
            model.meshes = [self.decoder.decode(address, node_materials)]
        model.animation = [None]
        return model

    def camera(self, animation: str, frame: float, initial: Optional[Camera3D] = None) -> Camera3D:
        script = self.archive.require_symbol(animation)
        camera = initial or Camera3D()
        start = JointPose(tracks=[
            camera.eye.x, camera.eye.y, camera.eye.z, 0.0,
            camera.at.x, camera.at.y, camera.at.z,
            camera.up.x, camera.up.z, camera.fov_y,
        ])
        pose = sample32(self.archive, script, frame, start)
        result = Camera3D(
            eye=Vec3(pose.tracks[0], pose.tracks[1], pose.tracks[2]),
            at=Vec3(pose.tracks[4], pose.tracks[5], pose.tracks[6]),
            up=Vec3(pose.tracks[8], 1.0, 0.0),
            fov_y=camera.fov_y,
            near_plane=camera.near_plane,
            far_plane=camera.far_plane,
        )
        if 1.0 < pose.tracks[9] < 179.0:
            result.fov_y = pose.tracks[9]
        return result

    def bind_fighter_animation(self, model: Model3D, file_id: int, wrapper: FighterWrapper) -> Model3D:
        scripts = animation_table(self.archive, Address(file_id, 0), len(model.nodes) + 1)
        if not scripts:
            raise RuntimeError("fighter animation table is empty")
        model.fighter_root = Node(scale=(1.0, 1.0, 1.0))
        model.fighter_root_animation = scripts[0]
        model.fighter_wrapper = wrapper
        model.animation = scripts[1:]
        model.fighter_animation = True
        model.is_fighter = True
        return model

    def transition(self, model: Model3D, file_id: int, wrapper: FighterWrapper, frame: float) -> Model3D:
        posed = copy.copy(model)
        posed.nodes = list(model.nodes)
        if posed.fighter_wrapper == wrapper and posed.fighter_root_animation:
            posed.fighter_root = apply_pose(
                posed.fighter_root,
                sample16(self.archive, posed.fighter_root_animation, frame, pose_from_node(posed.fighter_root)))
        else:
            posed.fighter_root = Node(scale=(1.0, 1.0, 1.0))
        posed.nodes = [
            apply_pose(node, sample16(self.archive, script, frame, pose_from_node(node))) if script else node
            for node, script in zip(posed.nodes, posed.animation)
        ]
        return self.bind_fighter_animation(posed, file_id, wrapper)

    def _decode_geometry(self, model: Model3D, layout: GeometryLayout) -> None:
        if layout == GeometryLayout.JointPairs:
            pairs = [node.display_list for node in model.nodes]
            model.parent_meshes, model.meshes = self.decoder.decode_joint_tree(pairs, model.materials)
            return
        display_lists = [node.display_list for node in model.nodes]
        model.meshes = self.decoder.decode_model_tree(
            display_lists, model.materials, layout == GeometryLayout.DisplayListLinks)


def matrix_sets(archive: RelocArchive, model: Model3D, frame: float) -> tuple[list[Matrix], list[Matrix]]:
    max_depth = 40
    parents = [identity() for _ in range(max_depth)]
    have_parent = [False] * max_depth
    model_matrix = multiply(
        multiply(translation((model.position.x, model.position.y, model.position.z)),
                 rotation((model.rotation.x, model.rotation.y, model.rotation.z))),
        scale((model.scale.x, model.scale.y, model.scale.z)))
    if model.fighter_root_animation:
        root = apply_pose(
            model.fighter_root,
            sample16(archive, model.fighter_root_animation, frame, pose_from_node(model.fighter_root)))
        if model.fighter_wrapper == FighterWrapper.TransN:
            start = sample16(archive, model.fighter_root_animation, 0, pose_from_node(model.fighter_root))
            delta = tuple(root.translate[i] - start.tracks[4 + i] for i in range(3))
            model_matrix = multiply(model_matrix, translation(delta))
        else:
            model_matrix = multiply(
                model_matrix,
                multiply(multiply(translation(root.translate), rotation(root.rotate)), scale(root.scale)))
    result: list[Matrix] = []
    result_parents: list[Matrix] = []
    for index, source in enumerate(model.nodes):
        node = source
        if index < len(model.animation) and model.animation[index]:
            sampler = sample16 if model.fighter_animation else sample32
            node = apply_pose(node, sampler(archive, model.animation[index], frame, pose_from_node(node)))
        local = multiply(multiply(translation(node.translate), rotation(node.rotate)), scale(node.scale))
        for depth in range(max(node.depth, 0), max_depth):
            have_parent[depth] = False
        parent = model_matrix
        if 0 < node.depth <= max_depth:
            ancestor = node.depth - 1
            while ancestor > 0 and not have_parent[ancestor]:
                ancestor -= 1
            if have_parent[ancestor]:
                parent = parents[ancestor]
        world = multiply(parent, local)
        if 0 <= node.depth < max_depth:
            parents[node.depth] = world
            have_parent[node.depth] = True
        result.append(world)
        result_parents.append(parent)
    return result, result_parents


def world_matrices(archive: RelocArchive, model: Model3D, frame: float) -> list[Matrix]:
    return matrix_sets(archive, model, frame)[0]


def sample_node_flags(archive: RelocArchive, model: Model3D, frame: float) -> list[int]:
    flags = [0] * len(model.nodes)
    for index, script in enumerate(model.animation):
        if script is None:
            continue
        sampler = sample16 if model.fighter_animation else sample32
        flags[index] = sampler(archive, script, frame, pose_from_node(model.nodes[index])).flags
    return flags


def sample_materials(archive: RelocArchive, model: Model3D, node_index: int, frame: float) -> list[MaterialPose]:
    poses: list[MaterialPose] = []
    if node_index >= len(model.materials):
        return poses
    anim_frame = max(frame - model.material_animation_start, 0.0)
    for material_index, source in enumerate(model.materials[node_index]):
        pose = MaterialPose()
        pose.colors[0] = source.primitive
        if source.light1:
            pose.colors[3] = source.light1
        if source.light2:
            pose.colors[4] = source.light2
        scripts = model.material_animation[node_index] if node_index < len(model.material_animation) else []
        if material_index < len(scripts) and scripts[material_index]:
            pose = sample_material(archive, scripts[material_index], anim_frame, pose)
        poses.append(pose)
    return poses
