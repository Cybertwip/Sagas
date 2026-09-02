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
    const auto menu_select = sagas::decode_fgm(assets, 158);
    assert(assets.exists("audio/B1_sounds2/wave_010.aiff"));
    assert(menu_select.end_tick == 24 && menu_select.voices.size() == 1);
    assert(menu_select.voices[0].wave == 10 && menu_select.voices[0].start_tick == 0);
    assert(menu_select.voices[0].pitch.size() == 4);
    assert(menu_select.voices[0].pitch[1].tick == 4 && menu_select.voices[0].pitch[2].tick == 7);
    assert(menu_select.voices[0].pitch[3].tick == 12);
    assert(std::abs(menu_select.voices[0].pitch[0].cents - -580) < 0.01f);
    assert(std::abs(menu_select.voices[0].pitch[3].cents - 1020) < 0.01f);
    const auto menu_scroll = sagas::decode_fgm(assets, 164);
    assert(menu_scroll.end_tick == 24 && menu_scroll.voices.size() == 1);
    assert(menu_scroll.voices[0].wave == 10 && menu_scroll.voices[0].pitch[1].tick == 8);
    const auto title_start = sagas::decode_fgm(assets, 157);
    assert(title_start.end_tick == 38 && title_start.voices.size() == 1);
    assert(title_start.voices[0].wave == 21 && title_start.voices[0].pitch[1].tick == 10);
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
    const auto room_background=scene_loader.model(
        "llMVCommonRoomBackgroundDObjDesc", {}, sagas::GeometryLayout::DisplayListLinks,
        "llMVCommonRoomBackgroundMObjSub");
    std::size_t translucent_room_shadows{};
    for (const auto& part : room_background.meshes) for (const auto& vertex : part.vertices)
        translucent_room_shadows += vertex.color.r < 8 && vertex.color.g < 8 &&
                                    vertex.color.b < 8 && vertex.color.a < 255 && vertex.translucent;
    assert(translucent_room_shadows == 21);
    const auto dump_model = [](const char* name, const sagas::Model3D& model) {
        std::size_t vertices{}, lit{}, textured{}, dark{}, translucent{}, low_alpha{};
        float min_y=1e9f, max_y=-1e9f, nx{}, ny{}, nz{};
        long color_r{}, color_g{}, color_b{}, color_a{};
        std::size_t normal_count{};
        for (std::size_t node=0; node<model.meshes.size(); ++node) {
            const auto& part=model.meshes[node];
            if (part.vertices.empty()) continue;
            std::size_t node_lit{}, node_tex{}, node_dark{}, node_alpha{};
            int min_a=255, max_a=0;
            for (const auto& v : part.vertices) {
                ++vertices;
                min_y=std::min(min_y,v.y); max_y=std::max(max_y,v.y);
                color_r+=v.color.r; color_g+=v.color.g; color_b+=v.color.b; color_a+=v.color.a;
                min_a=std::min(min_a,static_cast<int>(v.color.a));
                max_a=std::max(max_a,static_cast<int>(v.color.a));
                if (v.lit) { ++lit; ++node_lit; nx+=v.normal.x; ny+=v.normal.y; nz+=v.normal.z; ++normal_count; }
                if (v.texture) { ++textured; ++node_tex; }
                if (v.translucent) ++translucent;
                if (v.color.a<255) { ++low_alpha; ++node_alpha; }
                if (v.color.r<16 && v.color.g<16 && v.color.b<16) { ++dark; ++node_dark; }
            }
            std::cout << name << " node " << node << " depth=" << model.nodes[node].depth
                      << " verts=" << part.vertices.size() << " lit=" << node_lit
                      << " tex=" << node_tex << " dark=" << node_dark
                      << " a<255=" << node_alpha << " a=[" << min_a << "," << max_a << "]"
                      << " y=" << model.nodes[node].translate[1] << "\n";
        }
        if (normal_count) { nx/=normal_count; ny/=normal_count; nz/=normal_count; }
        const float inv=vertices?1.0f/static_cast<float>(vertices):0;
        std::cout << name << " total verts=" << vertices << " lit=" << lit << " tex=" << textured
                  << " dark=" << dark << " translucent=" << translucent << " a<255=" << low_alpha
                  << " avgRGBA=(" << color_r*inv << "," << color_g*inv << "," << color_b*inv << "," << color_a*inv << ")"
                  << " y=[" << min_y << "," << max_y << "] avgN=(" << nx << "," << ny << "," << nz << ")\n";
    };
    dump_model("room.background", room_background);
    dump_model("room.desk", scene_loader.model("llMVCommonRoomDeskDObjDesc", {}, sagas::GeometryLayout::Direct));
    dump_model("room.snap", scene_loader.model("llMVCommonRoomSnapDObjDesc", "llMVCommonRoomSnapAnimJoint",
                                              sagas::GeometryLayout::DisplayListLinks));
    dump_model("room.spotlight", scene_loader.display_list("llMVCommonRoomSpotlightDisplayList",
                                                          sagas::GeometryLayout::Direct,
                                                          "llMVCommonRoomSpotlightMObjSub"));
    dump_model("room.desk_ground", scene_loader.model("llMVCommonRoomDeskGroundDObjDesc", {},
                                                     sagas::GeometryLayout::DisplayListLinks,
                                                     "llMVCommonRoomDeskGroundMObjSub"));
    const auto fighter_scripts = animation_decoder.table({362, 0}, 25);
    assert(fighter_scripts[1]);
    const auto fighter_pose = animation_decoder.sample16(*fighter_scripts[1], 50);
    for (const auto value : fighter_pose.tracks) assert(std::isfinite(value));
    assert(std::abs(fighter_pose.tracks[0]) < 10.0f);
    assert(fighter_pose.tracks[7] > 0.01f && fighter_pose.tracks[7] < 10.0f);
    const auto mario_model = scene_loader.fighter_model("llMarioModelJointTreeDObjDesc", sagas::GeometryLayout::Direct);
    std::size_t mario_material_commands{};
    for (const auto& part:mario_model.meshes) mario_material_commands+=part.material_commands;
    assert(mario_material_commands>0);
    const auto mario_materials=sagas::n64::DisplayListDecoder(archive).materials({296,0},mario_model.nodes.size());
    std::size_t mario_material_count{};
    bool saw_material_image{}, saw_material_primitive{};
    for (const auto& joint:mario_materials) for (const auto& material:joint) {
        ++mario_material_count;
        saw_material_image|=material.image.has_value();
        saw_material_primitive|=material.set_primitive;
    }
    assert(mario_material_count>=14 && saw_material_image && saw_material_primitive);
    const auto boss_model = scene_loader.model("llBossModelJointTreeDObjDesc", {}, sagas::GeometryLayout::JointPairs);
    auto boss_animated=boss_model;
    const auto boss_scripts=animation_decoder.table({458,0},boss_animated.nodes.size()+1);
    boss_animated.fighter_root.scale={1,1,1};
    boss_animated.fighter_root_animation=boss_scripts.front();
    boss_animated.fighter_wrapper=sagas::Model3D::FighterWrapper::TransN;
    boss_animated.animation.assign(boss_scripts.begin()+1,boss_scripts.end());
    boss_animated.fighter_animation=true;
    auto mario_animated=mario_model;
    mario_animated.fighter_root.scale={1,1,1};
    mario_animated.fighter_root_animation=fighter_scripts.front();
    mario_animated.fighter_wrapper=sagas::Model3D::FighterWrapper::TransN;
    mario_animated.animation.assign(fighter_scripts.begin()+1,fighter_scripts.end());
    mario_animated.fighter_animation=true;
    sagas::Scene3DRenderer renderer(archive);
    for (float frame : {20.0f,70.0f,100.0f}) {
        const auto placed=renderer.placed_at_joint(mario_animated,frame,boss_animated,frame+280.0f,1);
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
    const sagas::LightingRig test_lights{};
    const auto front_lit=sagas::LightingSystem::shade({180,180,180,255},{-0.35f,0.72f,0.60f},
                                                       {0,0,1},test_lights);
    const auto back_lit=sagas::LightingSystem::shade({180,180,180,255},{0.35f,-0.72f,-0.60f},
                                                      {0,0,1},test_lights);
    assert(front_lit.r>back_lit.r && front_lit.g>back_lit.g);
    std::cout << "Sagas core tests passed\n";
}
