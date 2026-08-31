#include <sagas/Engine.hpp>
#include <sagas/N64.hpp>

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
    std::cout << "Sagas core tests passed\n";
}
