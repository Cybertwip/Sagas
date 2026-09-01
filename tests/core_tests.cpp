#include <sagas/Engine.hpp>
#include <sagas/N64.hpp>
#include <sagas/Scene3D.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    const sagas::AnimationClip clip{{0, 4}, {2, 8}, {4, 0}};
    assert(std::abs(clip.sample(1) - 6) < 0.001f);
    assert(std::abs(clip.sample(3) - 4) < 0.001f);

    sagas::PhysicsWorld world;
    world.set_ground(10);
    sagas::Body body{{0, 8}, {0, 20}, {1, 1}};
    world.step(std::span{&body, 1}, 1.0f);
    assert(body.grounded && body.position.y == 9 && body.velocity.y == 0);

    sagas::AssetRepository assets(SAGAS_DEFAULT_ASSET_ROOT);
    sagas::n64::RelocArchive archive(assets);
    const auto ground = archive.symbol("llMVOpeningStandoffGroundDisplayList");
    assert(ground);
    const auto mesh = sagas::n64::DisplayListDecoder(archive).decode(*ground);
    assert(mesh.commands > 0 && !mesh.vertices.empty() && mesh.vertices.size() % 3 == 0);
    const auto yoster = archive.symbol("llMVOpeningYosterGroundDObjDesc");
    assert(yoster);
    const auto nodes = sagas::n64::SkeletonDecoder(archive).decode(*yoster);
    assert(nodes.size() == 33);
    const auto animation = archive.symbol("llMVOpeningYosterGroundAnimJoint");
    assert(animation);
    sagas::n64::AnimationDecoder animation_decoder(archive);
    const auto scripts = animation_decoder.table(*animation, nodes.size());
    assert(scripts.size() == nodes.size());
    assert(scripts[2]);
    const auto at_start = animation_decoder.sample(*scripts[2], 0, animation_decoder.pose(nodes[2]));
    const auto at_middle = animation_decoder.sample(*scripts[2], 40, animation_decoder.pose(nodes[2]));
    for (const auto value : at_middle.tracks) assert(std::isfinite(value));
    assert(at_start.tracks != at_middle.tracks);
    sagas::Scene3DLoader scene_loader(archive);
    const auto fighter_scripts = animation_decoder.table({362, 0}, 25);
    assert(fighter_scripts[1]);
    const auto fighter_pose = animation_decoder.sample16(*fighter_scripts[1], 50);
    for (const auto value : fighter_pose.tracks) assert(std::isfinite(value));
    assert(std::abs(fighter_pose.tracks[0]) < 10.0f);
    assert(fighter_pose.tracks[7] > 0.01f && fighter_pose.tracks[7] < 10.0f);
    const auto mario_model = scene_loader.fighter_model("llMarioModelJointTreeDObjDesc", sagas::GeometryLayout::Direct);
    const auto boss_model = scene_loader.model("llBossModelJointTreeDObjDesc", {}, sagas::GeometryLayout::JointPairs);
    std::size_t mario_material_textures{};
    for (const auto& part:mario_model.meshes) for (const auto& vertex:part.vertices)
        if (vertex.texture) ++mario_material_textures;
    std::cout << "material-backed Mario vertices: " << mario_material_textures << '\n';
    auto boss_animated=boss_model;
    boss_animated.animation=animation_decoder.table({458,0},boss_animated.nodes.size());
    boss_animated.fighter_animation=true;
    auto mario_animated=mario_model;
    mario_animated.animation=fighter_scripts;
    mario_animated.fighter_animation=true;
    sagas::Scene3DRenderer renderer(archive);
    for (float frame : {20.0f,70.0f,100.0f}) {
        const auto placed=renderer.placed_at_joint(mario_animated,frame,boss_animated,frame+280.0f,3);
        assert(placed.root_transform);
        for (const float value : *placed.root_transform) assert(std::isfinite(value));
    }
    struct ModelCase { const char* descriptor; const char* animation; sagas::GeometryLayout layout; };
    const ModelCase opening_models[]{
        {"llMVCommonRoomBackgroundDObjDesc", "", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningYosterNestDObjDesc", "", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningYosterGroundDObjDesc", "llMVOpeningYosterGroundAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningCliffHillsDObjDesc", "", sagas::GeometryLayout::Direct},
        {"llMVOpeningCliffOcarinaDObjDesc", "llMVOpeningCliffOcarinaAnimJoint", sagas::GeometryLayout::Direct},
        {"llMVOpeningYamabukiLegsDObjDesc", "llMVOpeningYamabukiLegsAnimJoint", sagas::GeometryLayout::Direct},
        {"llMVOpeningYamabukiLegsShadowDObjDesc", "llMVOpeningYamabukiLegsShadowAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningYamabukiMBallDObjDesc", "llMVOpeningYamabukiMBallAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningSectorGreatFoxDObjDesc", "llMVOpeningSectorGreatFoxAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningStandoffLightningDObjDesc", "llMVOpeningStandoffLightningAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVCommonRoomLogoDObjDesc", "", sagas::GeometryLayout::DisplayListLinks},
        {"llBossModelJointTreeDObjDesc", "", sagas::GeometryLayout::JointPairs},
        {"llMarioModelJointTreeDObjDesc", "", sagas::GeometryLayout::Direct},
        {"llLinkModelJointTreeDObjDesc", "", sagas::GeometryLayout::Direct},
    };
    bool saw_lit{};
    bool saw_unlit{};
    bool saw_textured{};
    bool saw_repeated_clamp_tile{};
    for (const auto& item : opening_models) {
        const auto model = scene_loader.model(item.descriptor, item.animation, item.layout);
        std::size_t triangles{};
        for (const auto& part : model.meshes) {
            triangles += part.vertices.size() / 3;
            for (const auto& vertex : part.vertices) {
                assert(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z));
                assert(std::isfinite(vertex.u) && std::isfinite(vertex.v));
                if (vertex.lit) {
                    saw_lit=true;
                    // A lit N64 vertex stores a normal in RGB+A.  Its last
                    // byte is never surface opacity.
                    assert(vertex.color.a==255);
                } else saw_unlit=true;
                if (vertex.texture) {
                    saw_textured=true;
                    assert(vertex.texture->width>0 && vertex.texture->height>0);
                    assert(vertex.texture->rgba.size()==static_cast<std::size_t>(
                        vertex.texture->width*vertex.texture->height*4));
                    if (((vertex.texture_mode_s&2U)!=0 &&
                         vertex.texture_window_s>vertex.texture->width) ||
                        ((vertex.texture_mode_t&2U)!=0 &&
                         vertex.texture_window_t>vertex.texture->height))
                        saw_repeated_clamp_tile=true;
                }
            }
        }
        assert(triangles > 0);
    }
    assert(saw_lit && saw_unlit && saw_textured && saw_repeated_clamp_tile);
    std::cout << "Sagas core tests passed\n";
}
