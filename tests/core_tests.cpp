#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/N64.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>

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
    // Figatree streams can end in the final two bytes of a reloc file.
    const auto punch_bytes=archive.bytes(936);
    const auto last_half=static_cast<std::uint32_t>(punch_bytes.size()-2);
    assert(static_cast<std::uint16_t>(archive.s16({936,last_half}))==
        ((std::to_integer<unsigned>(punch_bytes[last_half])<<8) |
          std::to_integer<unsigned>(punch_bytes[last_half+1])));
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
    // The room beam is I8: intensity supplies alpha, including clear texels.
    const auto sunlight=scene_loader.display_list("llMVCommonRoomSunlightDisplayList",
                                                  sagas::GeometryLayout::DisplayListLinks);
    const auto& beam=sunlight.meshes.front().vertices.front();
    assert(beam.rdp.cull_mode==0x400U);
    assert(beam.translucent && beam.rdp.enabled && beam.rdp.alpha_test);
    assert(std::abs(beam.rdp.alpha_threshold-8.0f/255.0f)<1e-6f);
    assert(beam.texture && beam.texture->width==32 && beam.texture->height==32);
    bool has_clear_texel=false,has_partial_texel=false;
    for (std::size_t i=0;i<beam.texture->rgba.size();i+=4) {
        assert(beam.texture->rgba[i]==beam.texture->rgba[i+3]);
        has_clear_texel|=beam.texture->rgba[i+3]==0;
        has_partial_texel|=beam.texture->rgba[i+3]>0 && beam.texture->rgba[i+3]<255;
    }
    assert(has_clear_texel && has_partial_texel);
    for (const auto* name:{"Castle","Jungle","Hyrule","Zebes","Yoster","Pupupu","Sector","Yamabuki"}) {
        const auto stage=scene_loader.stage(std::string("llGR")+name+"MapMapHeader");
        for (const auto& layer:stage.layers)
            for (std::size_t node=0;node<layer.nodes.size();++node) if (layer.animation[node])
                for (int frame:{0,247,248,319})
                    (void)animation_decoder.sample(*layer.animation[node],static_cast<float>(frame),
                                                   animation_decoder.pose(layer.nodes[node]));
        std::size_t vertices=0;
        for (const auto& layer:stage.layers) for (const auto& part:layer.meshes) {
            assert(part.unsupported_commands==0);
            vertices+=part.vertices.size();
        }
        assert(vertices>0);
        assert(std::isfinite(stage.movie_player1.x) && std::isfinite(stage.movie_player1.y));
    }
    // RGBA32 tile-line stride counts one of the two TMEM banks. Treating
    // it as a packed source stride turned these 32x32 billboards into stripes.
    const auto nest=scene_loader.model("llMVOpeningYosterNestDObjDesc");
    std::size_t rgba_billboards=0;
    for (const auto& vertex:nest.meshes[2].vertices) if (vertex.texture) {
        assert(vertex.texture->width==32 && vertex.texture->height==32);
        ++rgba_billboards;
    }
    assert(rgba_billboards>0);
    const auto room_background=scene_loader.model(
        "llMVCommonRoomBackgroundDObjDesc", {}, sagas::GeometryLayout::DisplayListLinks,
        "llMVCommonRoomBackgroundMObjSub");
    std::size_t translucent_room_shadows{};
    std::size_t opaque_unlit_textured{};
    for (const auto& part : room_background.meshes) for (const auto& vertex : part.vertices) {
        const bool contact_shadow = vertex.color.r < 8 && vertex.color.g < 8 &&
                                    vertex.color.b < 8 && vertex.translucent;
        translucent_room_shadows += contact_shadow;
        if (!vertex.lit && vertex.texture && !contact_shadow) {
            assert(vertex.color.a == 255);
            ++opaque_unlit_textured;
        }
    }
    assert(translucent_room_shadows == 21);
    assert(opaque_unlit_textured > 200);
    const auto spotlight = scene_loader.display_list("llMVCommonRoomSpotlightDisplayList",
                                                    sagas::GeometryLayout::Direct,
                                                    "llMVCommonRoomSpotlightMObjSub");
    assert(!spotlight.meshes.empty());
    for (const auto& vertex : spotlight.meshes.front().vertices) {
        assert(vertex.texture);
        assert(vertex.color.a == 255);
    }
    sagas::SceneResourceManager resources(assets);
    resources.load_manifest("scenes/opening.sgscene");
    constexpr std::array<unsigned,19> source_starts{
        0,1335,1515,1605,1695,1785,1875,1965,2055,2145,
        2250,2500,2690,2880,3230,3420,3610,3975,4155};
    assert(resources.timeline().size()==source_starts.size());
    unsigned presentation_tick=0;
    for (std::size_t i=0;i<source_starts.size();++i) {
        assert(presentation_tick==source_starts[i]);
        presentation_tick+=resources.timeline()[i].duration;
    }
    assert(presentation_tick==4195);
    for (const auto& intro : std::array<std::pair<const char*,unsigned>,8>{{
        {"mario",367},{"donkey",390},{"link",413},{"samus",401},
        {"yoshi",455},{"kirby",426},{"fox",378},{"pikachu",485}}}) {
        const std::string bundle=std::string("intro.")+intro.first;
        resources.activate(bundle);
        const auto& stance=resources.model(bundle+".stance");
        assert(stance.fighter_wrapper==sagas::Model3D::FighterWrapper::None);
        assert(!stance.fighter_root_animation);
        assert(stance.animation==animation_decoder.table({intro.second,0},stance.nodes.size()));
        resources.release(bundle);
    }
    resources.activate("jungle");
    resources.activate("standoff");
    for (const auto* key:{"jungle.donkey","jungle.samus","standoff.mario","standoff.kirby"}) {
        const auto& actor=resources.model(key);
        for (std::size_t node=0;node<actor.nodes.size();++node) if (actor.animation[node])
            for (int frame:{0,30,100,319})
                (void)animation_decoder.sample16(*actor.animation[node],static_cast<float>(frame),
                                                animation_decoder.pose(actor.nodes[node]));
    }
    resources.activate("room.base");
    resources.activate("room.action");
    const auto& halo = resources.model("room.spotlight");
    assert(halo.emit_spotlight);
    assert(!halo.receive_lighting);
    assert(std::abs(halo.position.x + 1149.30f) < 0.1f);
    assert(resources.model("boss.pose1").nodes.size() == 25);
    assert(resources.model("mario.pickup").nodes.size() == 24);
    assert(resources.model("link.fall").nodes.size() == 29);
    const auto fighter_scripts = animation_decoder.table({362, 0}, 25);
    assert(fighter_scripts[1]);
    const auto fighter_pose = animation_decoder.sample16(*fighter_scripts[1], 50);
    for (const auto value : fighter_pose.tracks) assert(std::isfinite(value));
    assert(std::abs(fighter_pose.tracks[0]) < 10.0f);
    assert(fighter_pose.tracks[7] > 0.01f && fighter_pose.tracks[7] < 10.0f);
    const auto mario_spec=sagas::fighter_model_spec(sagas::FighterKind::Mario);
    const auto mario_model = scene_loader.fighter_model(mario_spec.descriptor,
                                                        sagas::GeometryLayout::Direct,
                                                        mario_spec.setup_parts);
    assert(mario_model.nodes.size()==24);
    bool mario_face_has_interior_uv=false;
    for (const auto& vertex:mario_model.meshes[8].vertices)
        if (vertex.texture && vertex.texture->width==32 && vertex.texture->height==32 &&
            vertex.u>0.1f && vertex.u<0.9f && vertex.v>0.1f && vertex.v<0.9f)
            mario_face_has_interior_uv=true;
    assert(mario_face_has_interior_uv);
    const auto pikachu_spec=sagas::fighter_model_spec(sagas::FighterKind::Pikachu);
    const auto pikachu=scene_loader.fighter_model(pikachu_spec.descriptor,
        sagas::GeometryLayout::Direct,pikachu_spec.setup_parts);
    std::size_t shared_joint_vertices=0;
    for (std::size_t node=0;node<pikachu.meshes.size();++node)
        for (const auto& vertex:pikachu.meshes[node].vertices) {
            assert(vertex.transform_node<pikachu.nodes.size());
            if (vertex.transform_node!=node) ++shared_joint_vertices;
        }
    assert(shared_joint_vertices==106);
    std::size_t mario_material_commands{};
    std::size_t mario_triangles{}, mario_rejected{}, mario_unsupported{};
    std::size_t mario_textured{}, mario_untextured{};
    for (const auto& part:mario_model.meshes) {
        mario_material_commands+=part.material_commands;
        mario_triangles+=part.vertices.size()/3;
        mario_rejected+=part.rejected_triangles;
        mario_unsupported+=part.unsupported_commands;
        for (const auto& vertex:part.vertices) {
            mario_textured+=vertex.texture!=nullptr;
            mario_untextured+=vertex.texture==nullptr;
        }
    }
    assert(mario_triangles==320 && mario_rejected==0 && mario_unsupported==0);
    assert(mario_textured==192 && mario_untextured==768);
    assert(mario_material_commands>0);
    std::size_t mario_material_count{};
    bool saw_material_image{}, saw_material_primitive{};
    for (const auto& joint:mario_model.materials) for (const auto& material:joint) {
        ++mario_material_count;
        saw_material_image|=material.image.has_value();
        saw_material_primitive|=material.set_primitive;
    }
    assert(mario_material_count>=14 && saw_material_image && saw_material_primitive);
    for (std::uint8_t value=0;value<static_cast<std::uint8_t>(sagas::FighterKind::Count);++value) {
        const auto spec=sagas::fighter_model_spec(static_cast<sagas::FighterKind>(value));
        const auto fighter=scene_loader.fighter_model(
            spec.descriptor,
            spec.joint_pairs ? sagas::GeometryLayout::JointPairs : sagas::GeometryLayout::Direct,
            spec.setup_parts);
        std::size_t triangles{}, rejected{}, unsupported{}, textured{}, untextured{};
        for (const auto* parts:{&fighter.meshes,&fighter.parent_meshes})
            for (const auto& part:*parts) {
                triangles+=part.vertices.size()/3;
                rejected+=part.rejected_triangles;
                unsupported+=part.unsupported_commands;
                for (const auto& vertex:part.vertices) {
                    textured+=vertex.texture!=nullptr;
                    untextured+=vertex.texture==nullptr;
                }
            }
        if (rejected || unsupported) std::cerr << spec.descriptor << ": rejected=" << rejected << " unsupported=" << unsupported << "\n";
        assert(triangles>0 && rejected==0 && unsupported==0);
        assert(textured>0 && untextured>0);
    }
    const auto boss_model = scene_loader.model("llBossModelJointTreeDObjDesc", {}, sagas::GeometryLayout::JointPairs);
    std::size_t boss_triangles{}, boss_rejected{}, boss_unsupported{}, boss_seam_triangles{};
    const auto count_boss_part=[&](const sagas::n64::Mesh& part) {
        boss_triangles+=part.vertices.size()/3;
        boss_rejected+=part.rejected_triangles;
        boss_unsupported+=part.unsupported_commands;
        for (std::size_t i=0;i+2<part.vertices.size();i+=3) {
            const auto binding=[](const sagas::n64::Vertex& vertex) {
                return std::pair{vertex.transform_node,vertex.transform_parent};
            };
            boss_seam_triangles+=binding(part.vertices[i])!=binding(part.vertices[i+1]) ||
                                 binding(part.vertices[i])!=binding(part.vertices[i+2]);
        }
    };
    for (const auto& part : boss_model.meshes) {
        count_boss_part(part);
    }
    for (const auto& part : boss_model.parent_meshes) {
        count_boss_part(part);
    }
    assert(boss_triangles==474 && boss_rejected==0 && boss_unsupported==0);
    assert(boss_seam_triangles==207);
    bool saw_boss_light{};
    for (const auto& part : boss_model.meshes)
        for (const auto& vertex : part.vertices)
            if (vertex.light1 && vertex.light1->r > 200) saw_boss_light = true;
    for (const auto& part : boss_model.parent_meshes)
        for (const auto& vertex : part.vertices)
            if (vertex.light1 && vertex.light1->r > 200) saw_boss_light = true;
    assert(saw_boss_light);
    auto boss_animated=boss_model;
    const auto boss_scripts=animation_decoder.table({458,0},boss_animated.nodes.size()+1);
    boss_animated.fighter_root.scale={1,1,1};
    boss_animated.fighter_root_animation=boss_scripts.front();
    boss_animated.fighter_wrapper=sagas::Model3D::FighterWrapper::XRotN;
    boss_animated.animation.assign(boss_scripts.begin()+1,boss_scripts.end());
    boss_animated.fighter_animation=true;
    auto mario_animated=mario_model;
    mario_animated.fighter_root.scale={1,1,1};
    mario_animated.fighter_root_animation=fighter_scripts.front();
    mario_animated.fighter_wrapper=sagas::Model3D::FighterWrapper::XRotN;
    mario_animated.animation.assign(fighter_scripts.begin()+1,fighter_scripts.end());
    mario_animated.fighter_animation=true;
    sagas::Scene3DRenderer renderer(archive);
    for (float frame : {20.0f,70.0f,100.0f}) {
        const auto placed=renderer.placed_at_joint(mario_animated,frame,boss_animated,frame+280.0f,1);
        assert(placed.root_transform);
        for (const float value : *placed.root_transform) assert(std::isfinite(value));
    }
    resources.activate("room.action");
    const auto& falling=resources.model("mario.fall");
    assert(falling.fighter_wrapper==sagas::Model3D::FighterWrapper::TransN);
    const auto release_position=renderer.fighter_position(falling,0);
    const auto midfall_position=renderer.fighter_position(falling,10);
    const auto landed_position=renderer.fighter_position(falling,70);
    assert(midfall_position.y<release_position.y-500);
    assert(std::abs(landed_position.y-release_position.y+1949.5f)<0.01f);
    assert(std::abs(renderer.fighter_position(falling,759).y-landed_position.y)<0.01f);
    const auto& link_drop=resources.model("link.fall");
    assert(std::abs(link_drop.position.y-4038.864014f)<0.01f);
    assert(renderer.fighter_position(link_drop,70).y<link_drop.position.y-1900);
    // The ROM's base shirt is green; costume 0 changes it to Mario red.
    const auto shirt=mario_model.materials[4][0].primitive;
    assert(shirt.r>180 && shirt.g<50 && shirt.b<50);
    for (const std::size_t glove : {18U,23U})
        for (const auto& vertex : mario_model.meshes[glove].vertices)
            assert(vertex.color.r==255 && vertex.color.g==255 && vertex.color.b==255);
    const auto pants=mario_model.materials[15][0].primitive;
    assert(pants.b>pants.r && pants.b>pants.g);
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
    const auto side_lit=sagas::LightingSystem::shade({180,180,180,255},{0.72f,0.35f,0},
                                                     {0,0,1},test_lights);
    // Two-sided lighting toward the key light: opposite winding of the same
    // face must not flicker darker as the camera moves.
    assert(front_lit.r==back_lit.r && front_lit.g==back_lit.g && front_lit.b==back_lit.b);
    assert(front_lit.r>side_lit.r);
    const auto early_room_rig=sagas::LightingSystem::opening_room_at(100);
    // A dim room still needs a useful ambient floor and directional light.
    // Regressing to the old double-dark values made every unlit face black.
    assert((early_room_rig.ambient.r/255.0f)*early_room_rig.ambient_intensity>0.15f);
    assert(early_room_rig.key.intensity>0.5f);
    const auto early_room_surface=sagas::LightingSystem::shade(
        {180,180,180,255},early_room_rig.key.direction,{0,0,1},early_room_rig);
    assert(early_room_surface.r>50 && early_room_surface.g>45 && early_room_surface.b>40);
    auto room_rig=sagas::LightingSystem::opening_room();
    sagas::LightingSystem::aim_opening_spotlight(room_rig,{ -1149.30f, 2247.12f, -3681.99f },1.0f);
    assert(room_rig.spot.enabled);
    assert(room_rig.spot.position.y>room_rig.spot.position.x);
    sagas::FighterBody mario;
    mario.kind = sagas::FighterKind::Mario;
    mario.attr = sagas::fighter_attributes(mario.kind);
    mario.position = {0, 10, 0};
    mario.grounded = false;
    mario.vel_air.y = 0;
    sagas::FighterPhysics::apply_gravity_clamp_tvel(mario, mario.attr.gravity, mario.attr.tvel_base);
    assert(mario.vel_air.y < 0 && mario.vel_air.y > -mario.attr.tvel_base);
    mario.stick_x = 80;
    mario.grounded = true;
    mario.position.y = 0;
    mario.vel_air = {};
    sagas::FighterPhysics::tick(mario, 0);
    assert(mario.grounded && mario.vel_ground > 0 && mario.lr == 1);
    sagas::FighterPhysics::jump(mario);
    assert(!mario.grounded && mario.vel_air.y > 1.0f);
    std::cout << "Sagas core tests passed\n";
}
