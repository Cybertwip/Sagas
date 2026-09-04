from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from .archive import RelocArchive
from .math3d import Matrix, Vec3, cross, dot, normalize, sub, transform, transform_normal
from .model import Model3D, matrix_sets, sample_materials, sample_node_flags
from .types import Color, Mesh, RasterImage, Vertex


@dataclass
class DrawnVertex:
    x: float
    y: float
    z: float
    nx: float
    ny: float
    nz: float
    r: float
    g: float
    b: float
    a: float
    u: float
    v: float


@dataclass
class DrawBatch:
    vertices: list[DrawnVertex] = field(default_factory=list)
    texture: Optional[RasterImage] = None
    texture_mode_s: int = 0
    texture_mode_t: int = 0
    texture_mask_s: int = 0
    texture_mask_t: int = 0
    texture_window_s: int = 1
    texture_window_t: int = 1
    lit: bool = False
    translucent: bool = False
    node_index: int = 0
    material_index: int = 0xFFFF
    missing_palette: bool = False
    unmatched_format: bool = False


def _matches(batch: DrawBatch, vertex: Vertex, node_index: int) -> bool:
    texture = vertex.texture
    return (
        batch.texture is texture
        and batch.texture_mode_s == vertex.texture_mode_s
        and batch.texture_mode_t == vertex.texture_mode_t
        and batch.texture_mask_s == vertex.texture_mask_s
        and batch.texture_mask_t == vertex.texture_mask_t
        and batch.texture_window_s == vertex.texture_window_s
        and batch.texture_window_t == vertex.texture_window_t
        and batch.lit == vertex.lit
        and batch.translucent == vertex.translucent
        and batch.node_index == node_index
        and batch.material_index == vertex.material_index
    )


def _emit(batch: DrawBatch, vertex: Vertex, position: Vec3, normal: Vec3, color: Color) -> None:
    batch.vertices.append(DrawnVertex(
        position.x, position.y, position.z,
        normal.x, normal.y, normal.z,
        color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0,
        vertex.u, vertex.v,
    ))


def pose_model(archive: RelocArchive, model: Model3D, frame: float,
               tint: Color = Color()) -> list[DrawBatch]:
    matrices, parent_matrices = matrix_sets(archive, model, frame)
    flags = sample_node_flags(archive, model, frame)
    batches: list[DrawBatch] = []
    hidden_depth = -1

    def render_mesh(mesh: Mesh, world: Matrix, node_index: int) -> None:
        poses = sample_materials(archive, model, node_index, frame)
        vertices = mesh.vertices
        for i in range(0, len(vertices) - 2, 3):
            tri = vertices[i:i + 3]
            def vertex_matrix(src: Vertex) -> Matrix:
                if src.transform_node < len(matrices):
                    return (parent_matrices[src.transform_node] if src.transform_parent
                            else matrices[src.transform_node])
                return world
            world_points = [transform(vertex_matrix(src), Vec3(src.x, src.y, src.z)) for src in tri]
            if model.is_fighter or model.fighter_animation:
                edges = [
                    length_of(sub(world_points[1], world_points[0])),
                    length_of(sub(world_points[2], world_points[1])),
                    length_of(sub(world_points[0], world_points[2])),
                ]
                if max(edges) > 20000.0:
                    continue
            face = normalize(cross(sub(world_points[1], world_points[0]),
                                   sub(world_points[2], world_points[0])))
            sampler = tri[0]
            if batches and _matches(batches[-1], sampler, node_index):
                batch = batches[-1]
            else:
                batch = DrawBatch(
                    texture=sampler.texture,
                    texture_mode_s=sampler.texture_mode_s,
                    texture_mode_t=sampler.texture_mode_t,
                    texture_mask_s=sampler.texture_mask_s,
                    texture_mask_t=sampler.texture_mask_t,
                    texture_window_s=sampler.texture_window_s,
                    texture_window_t=sampler.texture_window_t,
                    lit=sampler.lit and model.receive_lighting,
                    translucent=sampler.translucent or tint.a < 255 or model.additive,
                    node_index=node_index,
                    material_index=sampler.material_index,
                    missing_palette=bool(sampler.texture and sampler.texture.missing_palette),
                    unmatched_format=bool(sampler.texture and sampler.texture.unmatched_format),
                )
                batches.append(batch)
            for src, point in zip(tri, world_points):
                valid_normal = src.nx * src.nx + src.ny * src.ny + src.nz * src.nz > 1.0e-6
                if src.lit and valid_normal:
                    normal = normalize(transform_normal(vertex_matrix(src), Vec3(src.nx, src.ny, src.nz)))
                else:
                    normal = face
                surface = src.color
                if src.material_index < len(poses):
                    animated = (node_index < len(model.material_animation)
                                and src.material_index < len(model.material_animation[node_index])
                                and model.material_animation[node_index][src.material_index] is not None)
                    if animated:
                        prim = poses[src.material_index].colors[0]
                        surface = prim if src.lit else surface.modulate(prim)
                _emit(batch, src, point, normal, surface.modulate(tint))

    for node_index, node in enumerate(model.nodes):
        depth = node.depth
        if hidden_depth >= 0 and depth <= hidden_depth:
            hidden_depth = -1
        if hidden_depth >= 0:
            continue
        if flags[node_index] & 2:
            hidden_depth = depth
            continue
        world = matrices[node_index]
        if (flags[node_index] & 1) == 0 and node_index < len(model.parent_meshes) and model.parent_meshes[node_index].vertices:
            render_mesh(model.parent_meshes[node_index], parent_matrices[node_index], node_index)
        if (flags[node_index] & 1) == 0:
            render_mesh(model.meshes[node_index], world, node_index)
    return batches


def length_of(v: Vec3) -> float:
    return (dot(v, v)) ** 0.5


def collect_textures(model: Model3D) -> list[RasterImage]:
    seen: dict[str, RasterImage] = {}
    for mesh in model.meshes + model.parent_meshes:
        for vertex in mesh.vertices:
            if vertex.texture and vertex.texture.key not in seen:
                seen[vertex.texture.key] = vertex.texture
    return list(seen.values())


def model_stats(model: Model3D) -> dict[str, int]:
    triangles = 0
    textured = 0
    untextured = 0
    lit = 0
    missing = 0
    unmatched = 0
    material_commands = 0
    for mesh in model.meshes + model.parent_meshes:
        triangles += len(mesh.vertices) // 3
        material_commands += mesh.material_commands
        for vertex in mesh.vertices:
            if vertex.texture:
                textured += 1
                if vertex.texture.missing_palette:
                    missing += 1
                if vertex.texture.unmatched_format:
                    unmatched += 1
            else:
                untextured += 1
            if vertex.lit:
                lit += 1
    return {
        "nodes": len(model.nodes),
        "triangles": triangles,
        "textured_vertices": textured,
        "untextured_vertices": untextured,
        "lit_vertices": lit,
        "materials": sum(len(entry) for entry in model.materials),
        "material_commands": material_commands,
        "textures": len(collect_textures(model)),
        "missing_palette_vertices": missing,
        "unmatched_format_vertices": unmatched,
    }
