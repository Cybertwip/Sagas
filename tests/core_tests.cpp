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
    struct ModelCase { const char* descriptor; const char* animation; sagas::GeometryLayout layout; };
    const ModelCase opening_models[]{
        {"llMVOpeningYosterNestDObjDesc", "", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningYosterGroundDObjDesc", "llMVOpeningYosterGroundAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningCliffHillsDObjDesc", "", sagas::GeometryLayout::Direct},
        {"llMVOpeningCliffOcarinaDObjDesc", "llMVOpeningCliffOcarinaAnimJoint", sagas::GeometryLayout::Direct},
        {"llMVOpeningYamabukiLegsDObjDesc", "llMVOpeningYamabukiLegsAnimJoint", sagas::GeometryLayout::Direct},
        {"llMVOpeningYamabukiLegsShadowDObjDesc", "llMVOpeningYamabukiLegsShadowAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningYamabukiMBallDObjDesc", "llMVOpeningYamabukiMBallAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningSectorGreatFoxDObjDesc", "llMVOpeningSectorGreatFoxAnimJoint", sagas::GeometryLayout::DisplayListLinks},
        {"llMVOpeningStandoffLightningDObjDesc", "llMVOpeningStandoffLightningAnimJoint", sagas::GeometryLayout::DisplayListLinks},
    };
    for (const auto& item : opening_models) {
        const auto model = scene_loader.model(item.descriptor, item.animation, item.layout);
        std::size_t triangles{};
        for (const auto& part : model.meshes) triangles += part.vertices.size() / 3;
        assert(triangles > 0);
    }
    std::cout << "Sagas core tests passed\n";
}
