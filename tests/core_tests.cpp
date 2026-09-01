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
    const auto mario_debug = scene_loader.model("llMarioModelJointTreeDObjDesc", {}, sagas::GeometryLayout::Direct);
    for (const float frame : {0.0f, 20.0f, 100.0f}) {
        std::cout << "mario frame " << frame << '\n';
        for (std::size_t i = 0; i < 3; ++i) {
            const auto pose = animation_decoder.sample16(*fighter_scripts[i], frame,
                                                         animation_decoder.pose(mario_debug.nodes[i]));
            std::cout << "  " << i << " depth=" << mario_debug.nodes[i].depth
                      << " r=" << pose.tracks[0] << ',' << pose.tracks[1] << ',' << pose.tracks[2]
                      << " t=" << pose.tracks[4] << ',' << pose.tracks[5] << ',' << pose.tracks[6]
                      << " s=" << pose.tracks[7] << ',' << pose.tracks[8] << ',' << pose.tracks[9] << '\n';
        }
    }
    const auto boss_debug = scene_loader.model("llBossModelJointTreeDObjDesc", {}, sagas::GeometryLayout::JointPairs);
    const auto boss_scripts = animation_decoder.table({460, 0}, 26);
    std::cout << "boss descriptor\n";
    for (std::size_t i=0; i<boss_debug.nodes.size(); ++i) {
        const auto& node=boss_debug.nodes[i];
        std::cout << "  " << i << " depth=" << node.depth << " flags=" << node.flags << " t=" << node.translate[0] << ','
                  << node.translate[1] << ',' << node.translate[2] << " triangles="
                  << boss_debug.meshes[i].vertices.size()/3 << '\n';
    }
    for (std::size_t i=0; i<3; ++i) if (boss_scripts[i]) {
        const auto pose=animation_decoder.sample16(*boss_scripts[i],40,animation_decoder.pose(boss_debug.nodes[i]));
        std::cout << "boss pose " << i << " t=" << pose.tracks[4] << ',' << pose.tracks[5] << ',' << pose.tracks[6]
                  << " s=" << pose.tracks[7] << ',' << pose.tracks[8] << ',' << pose.tracks[9] << '\n';
    }
    auto boss_animated=boss_debug;
    boss_animated.animation=animation_decoder.table({458,0},boss_animated.nodes.size());
    boss_animated.fighter_animation=true;
    auto mario_animated=mario_debug;
    mario_animated.animation=fighter_scripts;
    mario_animated.fighter_animation=true;
    sagas::Scene3DRenderer debug_renderer(archive);
    for (float frame : {20.0f,70.0f,100.0f}) {
        const auto placed=debug_renderer.placed_at_joint(mario_animated,frame,boss_animated,frame+280.0f,1);
        std::cout << "attached " << frame << " root=" << (*placed.root_transform)[3] << ','
                  << (*placed.root_transform)[7] << ',' << (*placed.root_transform)[11] << '\n';
    }
    std::size_t mario_textured{},mario_opaque{},mario_vertices{};
    for (const auto& mesh : mario_debug.meshes) for (const auto& vertex : mesh.vertices) {
        ++mario_vertices;
        if (vertex.texture) {
            ++mario_textured;
            for (std::size_t i=3;i<vertex.texture->rgba.size();i+=4) if (vertex.texture->rgba[i]) { ++mario_opaque; break; }
        }
    }
    std::cout << "mario vertices=" << mario_vertices << " textured=" << mario_textured
              << " opaque-texture-refs=" << mario_opaque << '\n';
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
    for (const auto& item : opening_models) {
        std::cout << "loading " << item.descriptor << '\n';
        const auto model = scene_loader.model(item.descriptor, item.animation, item.layout);
        std::size_t triangles{};
        std::size_t textured{};
        float min_u=1e9f,max_u=-1e9f,min_v=1e9f,max_v=-1e9f;
        const sagas::n64::Vertex* sample{};
        for (const auto& part : model.meshes) {
            triangles += part.vertices.size() / 3;
            for (const auto& vertex : part.vertices) if (vertex.texture) {
                ++textured; sample=&vertex;
                min_u=std::min(min_u,vertex.u); max_u=std::max(max_u,vertex.u);
                min_v=std::min(min_v,vertex.v); max_v=std::max(max_v,vertex.v);
            }
        }
        std::cout << "  nodes: " << model.nodes.size() << " meshes: " << model.meshes.size()
                  << " triangles: " << triangles << " textured: " << textured << '\n';
        if (sample) std::cout << "  texture " << sample->texture->width << 'x' << sample->texture->height
                              << " uv " << min_u << ',' << min_v << ".." << max_u << ',' << max_v
                              << " mode " << unsigned(sample->texture_mode_s) << ',' << unsigned(sample->texture_mode_t)
                              << " mask " << unsigned(sample->texture_mask_s) << ',' << unsigned(sample->texture_mask_t) << '\n';
        assert(triangles > 0);
    }
    std::cout << "Sagas core tests passed\n";
}
