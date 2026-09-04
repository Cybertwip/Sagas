"""Headless decode checks against the same opening assets sagas/tests/core_tests.cpp uses."""

from __future__ import annotations

from pathlib import Path

from .animation import animation_table, pose_from_node, sample16, sample32
from .archive import RelocArchive
from .display_list import DisplayListDecoder
from .model import FIGHTER_LAYOUTS, FIGHTER_SETUP_PARTS, GeometryLayout, ModelLoader
from .pose import collect_textures, model_stats
from .scene import SceneTable
from .skeleton import decode_skeleton
from .types import Address


def run_selftest(assets: Path, scene_dir: Path) -> list[str]:
    failures: list[str] = []

    def check(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    archive = RelocArchive(assets)
    decoder = DisplayListDecoder(archive)
    loader = ModelLoader(archive)

    ground = archive.symbol("llMVOpeningStandoffGroundDisplayList")
    check(ground is not None, "missing standoff ground display list")
    if ground:
        mesh = decoder.decode(ground)
        check(mesh.commands > 0 and mesh.vertices and len(mesh.vertices) % 3 == 0,
              "standoff ground did not decode to triangles")

    yoster = archive.require_symbol("llMVOpeningYosterGroundDObjDesc")
    nodes = decode_skeleton(archive, yoster)
    check(len(nodes) == 33, f"yoster skeleton has {len(nodes)} nodes, expected 33")
    animation = archive.require_symbol("llMVOpeningYosterGroundAnimJoint")
    scripts = animation_table(archive, animation, len(nodes))
    check(len(scripts) == len(nodes) and scripts[2] is not None, "yoster animation table is incomplete")
    if scripts[2]:
        start = sample32(archive, scripts[2], 0.0, pose_from_node(nodes[2]))
        middle = sample32(archive, scripts[2], 40.0, pose_from_node(nodes[2]))
        check(all(value == value for value in middle.tracks), "yoster sample produced NaN")
        check(start.tracks != middle.tracks, "yoster animation did not change by frame 40")

    room = loader.model("llMVCommonRoomBackgroundDObjDesc", "", GeometryLayout.DisplayListLinks,
                        "llMVCommonRoomBackgroundMObjSub")
    translucent_shadows = 0
    opaque_unlit_textured = 0
    for part in room.meshes:
        for vertex in part.vertices:
            contact = vertex.color.r < 8 and vertex.color.g < 8 and vertex.color.b < 8 and vertex.translucent
            translucent_shadows += int(contact)
            if not vertex.lit and vertex.texture and not contact:
                check(vertex.color.a == 255, "unlit textured room vertex lost coverage")
                opaque_unlit_textured += 1
    check(translucent_shadows == 21, f"room contact shadows = {translucent_shadows}, expected 21")
    check(opaque_unlit_textured > 200, f"room opaque unlit textured vertices = {opaque_unlit_textured}")

    spotlight = loader.display_list("llMVCommonRoomSpotlightDisplayList", GeometryLayout.Direct,
                                    "llMVCommonRoomSpotlightMObjSub")
    check(spotlight.meshes and all(vertex.texture and vertex.color.a == 255
                                   for vertex in spotlight.meshes[0].vertices),
          "spotlight mesh is missing textures")

    mario = loader.fighter_model("llMarioModelJointTreeDObjDesc", GeometryLayout.Direct)
    check(len(mario.nodes) == 24, f"Mario compacted joint count = {len(mario.nodes)}, expected 24")
    check(sum(len(part.vertices) // 3 for part in mario.meshes) == 320,
          "Mario stateful display-list triangle count is incomplete")
    check(sum(part.rejected_triangles for part in mario.meshes) == 0,
          "Mario has rejected display-list triangles")
    mario_material_commands = sum(part.material_commands for part in mario.meshes)
    check(mario_material_commands > 0, "Mario model issued no MObj segment commands")
    mario_textures = collect_textures(mario)
    check(mario_textures, "Mario decoded no textures")
    check(not any(texture.missing_palette for texture in mario_textures),
          "Mario CI textures are missing MObj palettes")
    mario_materials = decoder.materials(Address(296, 0), len(mario.nodes))
    material_count = sum(len(joint) for joint in mario_materials)
    check(material_count >= 14, f"Mario MObjSub count = {material_count}, expected >= 14")
    check(any(material.image for joint in mario_materials for material in joint),
          "Mario materials have no image pointers")
    check(any(material.set_primitive for joint in mario_materials for material in joint),
          "Mario materials never set primitive color")

    for descriptor, setup_parts in FIGHTER_SETUP_PARTS.items():
        if descriptor == "llBossModelJointTreeDObjDesc":
            continue
        fighter = loader.fighter_model(
            descriptor, FIGHTER_LAYOUTS.get(descriptor, GeometryLayout.Direct), setup_parts)
        fighter_stats = model_stats(fighter)
        check(fighter_stats["triangles"] > 0, f"{descriptor} decoded no triangles")
        check(fighter_stats["textured_vertices"] > 0,
              f"{descriptor} decoded no textured vertices")
        check(fighter_stats["untextured_vertices"] > 0,
              f"{descriptor} ignored gSPTexture off")
        check(fighter_stats["missing_palette_vertices"] == 0,
              f"{descriptor} has CI vertices without a palette")
        check(fighter_stats["unmatched_format_vertices"] == 0,
              f"{descriptor} has unsupported texture formats")
        check(all(texture.source != texture.palette for texture in collect_textures(fighter)),
              f"{descriptor} sampled a TLUT as a color texture")

    fighter_scripts = animation_table(archive, Address(362, 0), 25)
    check(fighter_scripts[1] is not None, "Mario figatree table slot 1 is empty")
    if fighter_scripts[1]:
        pose = sample16(archive, fighter_scripts[1], 50.0)
        check(all(value == value for value in pose.tracks), "figatree sample produced NaN")
        check(abs(pose.tracks[0]) < 10.0, f"unexpected Mario rotX {pose.tracks[0]}")
        check(0.01 < pose.tracks[7] < 10.0, f"unexpected Mario scaX {pose.tracks[7]}")

    table = SceneTable(scene_dir / "opening.scene.tsv", scene_dir / "opening.sequence.tsv",
                       scene_dir / "opening.cues.tsv")
    check(len(table.segments) == 19, f"opening sequence has {len(table.segments)} segments, expected 19")
    check(table.total_duration == 3650, f"opening duration {table.total_duration}, expected 3650")
    room_bundle = table.build_bundle(loader, "room.base")
    check("room.background" in room_bundle, "room.base did not build room.background")
    stats = model_stats(room_bundle["room.background"])
    check(stats["triangles"] > 0, "room.background has no triangles")
    check(stats["textures"] > 0, "room.background decoded no textures")
    textures = collect_textures(room_bundle["room.background"])
    check(all(image.width > 0 and image.height > 0 and len(image.rgba) == image.width * image.height * 4
              for image in textures), "decoded texture RGBA size is wrong")

    boss = loader.model("llBossModelJointTreeDObjDesc", "", GeometryLayout.JointPairs)
    check(sum(len(part.vertices) // 3 for part in boss.meshes + boss.parent_meshes) == 474,
          "Master Hand stateful display-list triangle count is incomplete")
    check(sum(part.rejected_triangles for part in boss.meshes + boss.parent_meshes) == 0,
          "Master Hand has rejected display-list triangles")
    check(sum(part.unsupported_commands for part in boss.meshes + boss.parent_meshes) == 0,
          "Master Hand has unsupported display-list commands")
    boss_seam_triangles = 0
    for part in boss.meshes + boss.parent_meshes:
        for index in range(0, len(part.vertices) - 2, 3):
            bindings = {(vertex.transform_node, vertex.transform_parent)
                        for vertex in part.vertices[index:index + 3]}
            boss_seam_triangles += int(len(bindings) > 1)
    check(boss_seam_triangles > 0,
          "Master Hand lost its mixed-matrix RSP seam triangles")
    saw_boss_light = False
    for part in boss.meshes + boss.parent_meshes:
        for vertex in part.vertices:
            if vertex.light1 and vertex.light1.r > 200:
                saw_boss_light = True
    check(saw_boss_light, "Master Hand DLs never applied gSPLightColor (0xDB)")

    opening_models = [
        ("llMVOpeningYosterNestDObjDesc", "", GeometryLayout.DisplayListLinks),
        ("llMVOpeningCliffHillsDObjDesc", "", GeometryLayout.Direct),
        ("llMVOpeningSectorGreatFoxDObjDesc", "llMVOpeningSectorGreatFoxAnimJoint",
         GeometryLayout.DisplayListLinks),
        ("llBossModelJointTreeDObjDesc", "", GeometryLayout.JointPairs),
        ("llLinkModelJointTreeDObjDesc", "", GeometryLayout.Direct),
    ]
    saw_lit = saw_unlit = saw_textured = False
    for descriptor, animation, layout in opening_models:
        model = loader.model(descriptor, animation, layout)
        triangles = 0
        for part in model.meshes:
            triangles += len(part.vertices) // 3
            for vertex in part.vertices:
                check(vertex.x == vertex.x and vertex.u == vertex.u, f"non-finite vertex in {descriptor}")
                if vertex.lit:
                    saw_lit = True
                    check(vertex.color.a == 255, f"lit vertex in {descriptor} used alpha as opacity")
                else:
                    saw_unlit = True
                if vertex.texture:
                    saw_textured = True
                    check(len(vertex.texture.rgba) == vertex.texture.width * vertex.texture.height * 4,
                          f"texture RGBA size mismatch in {descriptor}")
        check(triangles > 0, f"{descriptor} decoded no triangles")
    check(saw_lit and saw_unlit and saw_textured, "opening models did not exercise lit/unlit/textured paths")
    return failures
