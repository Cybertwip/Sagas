#include <sagas/OpeningMotionAudio.hpp>
#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/FighterSourceData.hpp>
#include <sagas/FighterAttackData.hpp>
#include <sagas/N64.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    sagas::InputState held;
    held.right=true; held.stick_x=80; held.shield_held=true; held.jump_pressed=true;
    sagas::InputState polled;
    polled.latch_edges(held);
    assert(polled.jump_pressed && !polled.right && polled.stick_x==0);
    held.clear_edges();
    assert(!held.jump_pressed && held.right && held.stick_x==80 && held.shield_held);
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
    assert(sagas::fgm_pitch_cents(menu_select.voices[0],3)==menu_select.voices[0].pitch[0].cents);
    assert(sagas::fgm_pitch_cents(menu_select.voices[0],4)==menu_select.voices[0].pitch[1].cents);
    assert(sagas::fgm_pitch_cents(menu_select.voices[0],12)==1020);
    sagas::FgmVoice envelope_voice;
    envelope_voice.envelope={{0,0},{10,1},{20,0}};
    assert(sagas::fgm_envelope(envelope_voice,5)==.5f);
    assert(sagas::fgm_envelope(envelope_voice,15)==.5f);
    assert(sagas::fgm_envelope(envelope_voice,25)==0);
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
    // Selected animations for Ness/Pikachu include XRotN ahead of common
    // joints. Verify the stream offset and world-space pose across the clip.
    sagas::Scene3DRenderer pose_renderer(archive);
    for (unsigned i=0;i<sagas::fighter_source_data.size();++i) {
        const auto& data=sagas::fighter_source_data[i];
        const auto selected=scene_loader.fighter_motion(static_cast<sagas::FighterKind>(i),
                                                       data.selected,data.selected_flags);
        const bool wrapper=i==static_cast<unsigned>(sagas::FighterKind::Ness) ||
                           i==static_cast<unsigned>(sagas::FighterKind::Pikachu);
        assert((selected.fighter_wrapper==sagas::Model3D::FighterWrapper::XRotN)==wrapper);
        const auto raw=animation_decoder.table({data.selected,0},selected.nodes.size()+(wrapper?1:0));
        assert(selected.animation.front()==raw[wrapper?1:0]);
        if (wrapper) assert(selected.fighter_root_animation==raw.front());
        for (float frame:{0.f,8.f,30.f,60.f,100.f,150.f})
            for (auto joint:selected.source_joint_ids) {
                const auto point=pose_renderer.joint_point(selected,frame,joint,{});
                assert(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
                assert(std::abs(point.x)<3000 && std::abs(point.y)<3000 && std::abs(point.z)<3000);
            }
    }
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
    // Every cinematic cast is loaded, including the final scene's eight fighters.
    for (const auto* bundle:{"run","clash","cliff","yamabuki","yoster"}) {
        resources.activate(bundle);
        if (std::string_view(bundle)=="run" || std::string_view(bundle)=="clash")
            for (const auto* name:{"mario","fox","donkey","samus","link","yoshi","kirby","pikachu"}) {
                const auto& actor=resources.model(std::string(bundle)+"."+name);
                assert(actor.is_fighter && !actor.animation.empty());
                for (std::size_t node=0;node<actor.nodes.size();++node) if (actor.animation[node])
                    (void)animation_decoder.sample16(*actor.animation[node],105,animation_decoder.pose(actor.nodes[node]));
            }
        resources.release(bundle);
    }
    const auto yoshi_spec=sagas::fighter_model_spec(sagas::FighterKind::Yoshi);
    const auto yoshi_base=scene_loader.fighter_model(yoshi_spec.descriptor,sagas::GeometryLayout::JointPairs,yoshi_spec.setup_parts);
    const auto yoshi_grab=scene_loader.fighter_model(yoshi_spec.descriptor,sagas::GeometryLayout::JointPairs,yoshi_spec.setup_parts,0x18000001);
    assert(yoshi_grab.nodes.size()==yoshi_base.nodes.size()+2);
    const auto grab_scripts=animation_decoder.table({1876,0},yoshi_grab.nodes.size());
    for (std::size_t node=0;node<grab_scripts.size();++node) if (grab_scripts[node])
        for (const int frame:{0,5,15,23}) {
            const auto pose=animation_decoder.sample16(*grab_scripts[node],frame,animation_decoder.pose(yoshi_grab.nodes[node]));
            for (const auto value:pose.tracks) assert(std::isfinite(value));
        }
    bool jab3_voice=false,jab3_swing=false;
    for (const auto& event:sagas::opening_motion_sounds) {
        assert(!sagas::decode_fgm(assets,event.fgm).voices.empty());
        jab3_voice|=event.motion==608 && event.frame==0 && event.fgm==429;
        jab3_swing|=event.motion==608 && event.frame==3 && event.fgm==42;
    }
    assert(jab3_voice && jab3_swing);
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
    mario.stick_x=0;
    for (int frame=0;frame<180;++frame) sagas::FighterPhysics::tick(mario,0);
    assert(mario.grounded && mario.position.y==0 && mario.jumps_used==0);
    mario.jump_pressed=true;
    sagas::FighterPhysics::tick(mario,0);
    assert(mario.grounded && mario.status==sagas::FighterStatus::KneeBend);
    for (int frame=1;frame<mario.attr.knee_bend;++frame) sagas::FighterPhysics::tick(mario,0);
    assert(!mario.grounded && mario.vel_air.y>0);
    const std::array<sagas::CollisionSegment,2> platforms{{
        {{-1000,0},{1000,0},0,0,false},{{-100,500},{100,500},0,0,true}}};
    mario.position={0,500,0}; mario.vel_air={}; mario.grounded=true;
    mario.status=sagas::FighterStatus::Wait; mario.stick_y=-80; mario.tap_stick_y=0;
    sagas::FighterPhysics::tick(mario,platforms);
    assert(!mario.grounded && mario.position.y<500);
    mario.stick_y=0;
    for (int frame=0;frame<100;++frame) sagas::FighterPhysics::tick(mario,platforms);
    assert(mario.grounded && mario.position.y==0);
    mario.position.x=999; mario.stick_x=80;
    sagas::FighterPhysics::tick(mario,platforms);
    assert(!mario.grounded);
    sagas::FighterPhysics::tick(mario,platforms);
    assert(mario.position.y<0);
    std::array<sagas::FighterBody,2> fighters{};
    for (auto& fighter:fighters) fighter.attr=sagas::fighter_attributes(sagas::FighterKind::Mario);
    const std::array<sagas::AttackVolume,1> attack{{{0,{0,160,0},160,8,45,100,0,10}}};
    auto hits=sagas::FighterCombat::resolve(fighters,attack);
    assert(hits.size()==1 && fighters[1].damage==8 && fighters[1].hitstun>0);
    assert(fighters[1].vel_damage.y>0 && fighters[0].hitlag==6);
    assert(sagas::FighterCombat::resolve(fighters,attack).empty());
    fighters[0].hit_mask=0; fighters[1].status=sagas::FighterStatus::Shield;
    const float damage=fighters[1].damage;
    hits=sagas::FighterCombat::resolve(fighters,attack);
    assert(hits.size()==1 && fighters[1].damage==damage && fighters[1].shield==47);
    // Landing must preserve horizontal knockback independently of input.
    auto& sliding=fighters[1];
    sliding.hitlag=0; sliding.status=sagas::FighterStatus::Hitstun;
    sliding.position={0,1,0}; sliding.grounded=false;
    sliding.vel_air={}; sliding.vel_damage={20,-5,0};
    sagas::FighterPhysics::tick(sliding,0);
    assert(sliding.grounded && sliding.vel_damage.x>0);
    const float landed_x=sliding.position.x,landed_speed=sliding.vel_damage.x;
    sagas::FighterPhysics::tick(sliding,0);
    assert(sliding.position.x>landed_x);
    assert(std::abs(sliding.vel_damage.x-(landed_speed-sliding.floor_friction*sliding.attr.traction*.25f))<.001f);
    sagas::FighterBody jumping;
    jumping.attr=sagas::fighter_attributes(jumping.kind);
    jumping.jump_button=true;
    sagas::FighterPhysics::jump(jumping);
    assert(std::abs(jumping.vel_air.y-(77*.7f+26))<.001f);
    jumping.grounded=true; jumping.short_hop=true;
    sagas::FighterPhysics::jump(jumping);
    assert(std::abs(jumping.vel_air.y-(45*.7f+26))<.001f);
    for (const auto kind:{sagas::FighterKind::Kirby,sagas::FighterKind::Purin}) {
        jumping={};jumping.kind=kind;jumping.attr=sagas::fighter_attributes(kind);
        sagas::FighterPhysics::jump(jumping);
        sagas::FighterPhysics::jump(jumping);
        assert(std::abs(jumping.vel_air.y-jumping.attr.jump_vel_y*jumping.attr.aerial_height)<.001f);
        const std::array<float,4> expected=kind==sagas::FighterKind::Kirby?
            std::array<float,4>{60,52,47,40}:std::array<float,4>{60,40,20,0};
        for (const auto velocity:expected) {
            sagas::FighterPhysics::jump(jumping); assert(jumping.vel_air.y==velocity);
        }
        assert(jumping.jumps_used==6);
        sagas::FighterPhysics::jump(jumping); assert(jumping.jumps_used==6);
    }
    // Source normal-floor material (4) versus slippery material (1).
    sagas::FighterBody friction_body;friction_body.attr=sagas::fighter_attributes(friction_body.kind);
    friction_body.vel_ground=40;
    for (const float speed:{34.f,28.f,22.f,16.f,10.f,4.f,0.f}) {
        sagas::FighterPhysics::tick(friction_body,0);
        assert(std::abs(friction_body.vel_ground-speed)<.001f);
    }
    const sagas::CollisionSegment slippery{{-1000,0},{1000,0},0,3,false};
    friction_body.vel_ground=40;
    sagas::FighterPhysics::tick(friction_body,std::span<const sagas::CollisionSegment>(&slippery,1));
    assert(std::abs(friction_body.vel_ground-38.5f)<.001f);
    // Sloped ground velocity follows the tangent, retaining its magnitude.
    const sagas::CollisionSegment slope{{-1000,-500},{1000,500},0,0,false};
    friction_body={};friction_body.attr=sagas::fighter_attributes(friction_body.kind);
    friction_body.status=sagas::FighterStatus::Run;friction_body.stick_x=80;
    sagas::FighterPhysics::tick(friction_body,std::span<const sagas::CollisionSegment>(&slope,1));
    assert(friction_body.grounded && std::abs(friction_body.position.y-friction_body.position.x*.5f)<.001f);
    assert(std::abs(std::hypot(friction_body.position.x,friction_body.position.y)-44.f)<.001f);
    // Down held through the apex cannot trigger fast-fall; a new tap can.
    sagas::FighterBody fastfall_body;fastfall_body.attr=sagas::fighter_attributes(fastfall_body.kind);
    fastfall_body.position.y=1000;fastfall_body.grounded=false;fastfall_body.status=sagas::FighterStatus::Fall;
    fastfall_body.vel_air.y=-1;fastfall_body.stick_y=-80;fastfall_body.tap_stick_y=10;
    sagas::FighterPhysics::tick(fastfall_body,0);assert(!fastfall_body.fastfall);
    fastfall_body.tap_stick_y=0;sagas::FighterPhysics::tick(fastfall_body,0);
    assert(fastfall_body.fastfall && fastfall_body.vel_air.y==-70 && fastfall_body.tap_stick_y==255);
    // Every roster member exposes all five aerials and source hitbox windows.
    for (unsigned kind=0;kind<12;++kind) for (int direction=0;direction<5;++direction) {
        sagas::FighterBody air;air.kind=static_cast<sagas::FighterKind>(kind);air.attr=sagas::fighter_attributes(air.kind);
        air.position.y=1000;air.grounded=false;air.status=sagas::FighterStatus::Fall;air.lr=-1;
        air.stick_x=direction==1?-80:direction==2?80:0;air.stick_y=direction==3?80:direction==4?-80:0;
        assert(sagas::FighterCombat::start_aerial(air,true));
        assert(air.aerial_attack==direction && air.lr==-1);
        assert(std::any_of(sagas::source_jab_hitboxes.begin(),sagas::source_jab_hitboxes.end(),[&](const auto& box){return box.kind==kind && box.motion==air.attack_motion;}));
        assert(!sagas::FighterCombat::start_aerial(air,true));
        sagas::FighterCombat::advance_jab(air,false,true);
        assert(air.status==sagas::FighterStatus::Fall && air.attack_motion==0);
    }
    // An active fair lands into its authored landing clip, unless Z-cancelled.
    for (const bool cancel:{false,true}) {
        sagas::FighterBody air;air.attr=sagas::fighter_attributes(air.kind);air.grounded=false;
        air.status=sagas::FighterStatus::Fall;air.stick_x=80;
        assert(sagas::FighterCombat::start_aerial(air,true));
        air.action_frame=11;air.shield_tics=cancel?0:255;air.position.y=1;air.vel_air={20,-10,0};
        sagas::FighterPhysics::tick(air,0);
        assert(air.grounded && air.status==sagas::FighterStatus::Land && air.attack_motion==0);
        assert(cancel?air.landing_motion==0:air.landing_motion==627);
        assert(air.vel_ground>0); // landing no longer restores an old dash velocity
    }
    // Grabs bypass a shield, create reciprocal capture links, and deal no jab damage.
    std::array<sagas::FighterBody,2> grabbing{};
    for (auto& body:grabbing) body.attr=sagas::fighter_attributes(body.kind);
    grabbing[1].status=sagas::FighterStatus::Shield;
    assert(sagas::FighterCombat::start_grab(grabbing[0],true));
    sagas::AttackVolume grab{0,{0,160,0},160,1,361,100,0,0,0,true};
    assert(sagas::FighterCombat::resolve(grabbing,std::span<const sagas::AttackVolume>(&grab,1)).size()==1);
    assert(grabbing[0].capture_target==1 && grabbing[1].captured_by==0);
    assert(grabbing[1].status==sagas::FighterStatus::Captured && grabbing[1].damage==0 && grabbing[1].shield==55);
    sagas::FighterCombat::advance_jab(grabbing[0],true,false);
    assert(grabbing[0].status==sagas::FighterStatus::CatchWait);
    sagas::InputState edges;edges.attack_pressed=edges.grab_pressed=edges.shield_pressed=edges.pointer_released=true;
    sagas::InputState latched;latched.latch_edges(edges);latched.clear_edges();
    assert(!latched.attack_pressed && !latched.grab_pressed && !latched.shield_pressed && !latched.pointer_released);
    // Ground crouch holds its facing and blocks walking until released.
    sagas::FighterBody crouching;crouching.attr=sagas::fighter_attributes(crouching.kind);
    crouching.stick_y=-53;crouching.stick_x=80;
    sagas::FighterPhysics::tick(crouching,0);
    assert(crouching.status==sagas::FighterStatus::Crouch && crouching.position.x==0);
    crouching.status=sagas::FighterStatus::CrouchWait;crouching.stick_y=-50;
    sagas::FighterPhysics::tick(crouching,0);assert(crouching.status==sagas::FighterStatus::CrouchWait);
    crouching.stick_y=-49;sagas::FighterPhysics::tick(crouching,0);
    assert(crouching.status==sagas::FighterStatus::CrouchEnd);
    // Fox up-air's group change must reconnect after the 2-damage setup hit.
    const auto fox_uair=sagas::fighter_source_data[9].attack_air[3];
    std::array<sagas::FighterBody,2> multi{};
    for (auto& body:multi) body.attr=sagas::fighter_attributes(body.kind);
    unsigned first_epoch=0;
    for (const unsigned frame:{6U,12U}) {
        unsigned contacts=0;
        for (const auto& box:sagas::source_jab_hitboxes) if (box.motion==fox_uair && box.begin<=frame && frame<box.end) {
            if (frame==6) first_epoch=box.epoch;
            else assert(first_epoch!=box.epoch);
            sagas::AttackVolume volume{0,{0,160,0},160,box.damage,box.angle,box.growth,box.weight,box.base,0,false,box.group,box.epoch};
            contacts+=sagas::FighterCombat::resolve(multi,std::span<const sagas::AttackVolume>(&volume,1)).size();
        }
        assert(contacts==1); // paired hitboxes cannot deal the same hit twice
        multi[0].hitlag=multi[1].hitlag=0;
    }
    assert(multi[1].damage==15);
    // Explicit record refreshes split Fox's drill into seven reconnect windows.
    std::vector<unsigned> drill_epochs;
    for (const auto& box:sagas::source_jab_hitboxes) if (box.motion==sagas::fighter_source_data[9].attack_air[4] && box.id==0)
        drill_epochs.push_back(box.epoch);
    assert(drill_epochs.size()==7);
    assert(std::adjacent_find(drill_epochs.begin(),drill_epochs.end())==drill_epochs.end());
    for (unsigned kind=0;kind<12;++kind) {
        const auto& data=sagas::fighter_source_data[kind];
        for (const unsigned clip:{data.attack_air[0],data.attack_air[1],data.grab[0]}) {
            const auto actor=scene_loader.fighter_motion(static_cast<sagas::FighterKind>(kind),clip,sagas::fighter_motion_flags(clip));
            if (kind==2 && clip!=data.grab[0]) {
                assert(actor.fighter_wrapper==sagas::Model3D::FighterWrapper::XRotN);
                const auto raw=animation_decoder.table({clip,0},actor.nodes.size()+1);
                assert(actor.animation.front()==raw[1]);
            }
            for (const auto& box:sagas::source_jab_hitboxes) if (box.kind==kind && box.motion==clip) {
                const bool present=box.joint==0 || std::find(actor.source_joint_ids.begin(),actor.source_joint_ids.end(),box.joint)!=actor.source_joint_ids.end();
                if (!present) std::cerr<<"Missing source hitbox joint: kind "<<kind<<" clip "<<clip<<" joint "<<box.joint<<'\n';
                assert(present);
            }
        }
    }
    // Actual Mario grab geometry: auxiliary joint 28 is enabled by motion flags.
    {
        const unsigned clip=sagas::fighter_source_data[1].grab[0];
        auto actor=scene_loader.fighter_motion(sagas::FighterKind::Mario,clip,sagas::fighter_motion_flags(clip));
        actor.rotation.y=1.57079632679f;actor.scale={1.12f,1.12f,1.12f};
        std::array<sagas::FighterBody,2> bodies{};
        for (auto& body:bodies) body.attr=sagas::fighter_attributes(body.kind);
        bodies[1].position.x=220;assert(sagas::FighterCombat::start_grab(bodies[0],true));
        std::vector<sagas::AttackVolume> volumes;
        for (const auto& box:sagas::source_jab_hitboxes) if (box.kind==1 && box.motion==clip && box.begin==6) {
            auto point=pose_renderer.joint_point(actor,6,box.joint,{static_cast<float>(box.x),static_cast<float>(box.y),static_cast<float>(box.z)});
            volumes.push_back({0,point,box.radius*.5f*1.12f,1,361,100,0,0,0,true,box.group,box.epoch});
        }
        assert(!volumes.empty());
        assert(sagas::FighterCombat::resolve(bodies,volumes).size()==1);
        assert(bodies[1].status==sagas::FighterStatus::Captured);
    }
    // Held directional A selects tilts; a fresh strong tap selects smashes first.
    for (unsigned kind=0;kind<12;++kind) for (unsigned direction=0;direction<3;++direction) {
        sagas::FighterBody tilt;tilt.kind=static_cast<sagas::FighterKind>(kind);tilt.attr=sagas::fighter_attributes(tilt.kind);
        tilt.stick_x=direction==0?40:0;tilt.stick_y=direction==1?40:direction==2?-40:0;
        assert(sagas::FighterCombat::start_tilt(tilt,true));
        assert(tilt.attack_motion==sagas::fighter_source_data[kind].tilt[direction==0?2:direction==1?5:6]);
        const bool has_tilt=std::any_of(sagas::source_jab_hitboxes.begin(),sagas::source_jab_hitboxes.end(),
            [&](const auto& box){return box.kind==kind && box.motion==tilt.attack_motion;});
        if (!has_tilt) std::cerr<<"Missing tilt hitboxes kind "<<kind<<" motion "<<tilt.attack_motion<<'\n';
        assert(has_tilt);
    }
    // Every fighter exposes a running attack with authored contact windows.
    for (unsigned kind=0;kind<12;++kind) {
        sagas::FighterBody runner;runner.kind=static_cast<sagas::FighterKind>(kind);
        runner.attr=sagas::fighter_attributes(runner.kind);runner.status=sagas::FighterStatus::Run;
        runner.vel_ground=runner.attr.run_speed;
        assert(sagas::FighterCombat::start_dash_attack(runner,true));
        assert(std::any_of(sagas::source_jab_hitboxes.begin(),sagas::source_jab_hitboxes.end(),
            [&](const auto& box){return box.kind==kind && box.motion==runner.attack_motion;}));
        assert(!sagas::FighterCombat::start_dash_attack(runner,true));
    }
    // The table index, rather than misleading asset filenames, determines
    // damage reactions. Low-power air hits and vertical launches differ.
    for (unsigned kind=0;kind<12;++kind) for (int mode=0;mode<3;++mode) {
        std::array<sagas::FighterBody,2> bodies{};
        bodies[1].kind=static_cast<sagas::FighterKind>(kind);
        for (auto& body:bodies) body.attr=sagas::fighter_attributes(body.kind);
        bodies[1].grounded=mode!=1;
        sagas::AttackVolume hit{0,{0,bodies[1].attr.height*.5f,0},100,1,mode==2?90:0,0,0,mode==2?80:10,0};
        assert(sagas::FighterCombat::resolve(bodies,std::span<const sagas::AttackVolume>(&hit,1)).size()==1);
        const auto& victim=bodies[1];
        assert(victim.lr==-1 && victim.status==sagas::FighterStatus::Hitstun);
        assert(victim.damage_motion==sagas::fighter_source_data[kind].damage_reactions[mode==0?3:mode==1?9:16]);
        assert(victim.damage_tumble==(mode!=0));
        auto reaction=scene_loader.fighter_motion(victim.kind,victim.damage_motion,sagas::fighter_motion_flags(victim.damage_motion));
        assert(!reaction.animation.empty());
    }
    // Fox forward-smash TransN displacement belongs to physics, in either facing.
    const auto fox_smash=sagas::fighter_source_data[9].smash[0];
    const auto fox_motion=scene_loader.fighter_motion(sagas::FighterKind::Fox,fox_smash,sagas::fighter_motion_flags(fox_smash));
    assert(fox_motion.fighter_wrapper==sagas::Model3D::FighterWrapper::TransN && fox_motion.fighter_root_animation);
    const auto initial=animation_decoder.pose(fox_motion.fighter_root);
    const auto root0=animation_decoder.sample16(*fox_motion.fighter_root_animation,0,initial);
    const auto root20=animation_decoder.sample16(*fox_motion.fighter_root_animation,20,initial);
    for (const int facing:{-1,1}) {
        sagas::FighterBody fox;fox.kind=sagas::FighterKind::Fox;fox.attr=sagas::fighter_attributes(fox.kind);fox.lr=facing;
        fox.stick_x=80*facing;fox.tap_stick_x=0;assert(sagas::FighterCombat::start_smash(fox,true));
        const sagas::CollisionSegment floor{{-10000,0},{10000,0},0,0,false};
        for (int frame=1;frame<=20;++frame) {
            fox.action_frame=frame;
            sagas::FighterPhysics::tick(fox,std::span<const sagas::CollisionSegment>(&floor,1),[&](const auto& body)->std::optional<sagas::Vec3> {
                const auto before=animation_decoder.sample16(*fox_motion.fighter_root_animation,body.action_frame-1,initial);
                const auto after=animation_decoder.sample16(*fox_motion.fighter_root_animation,body.action_frame,initial);
                return sagas::Vec3{(after.tracks[6]-before.tracks[6])*body.lr*body.attr.size,0,0};
            });
        }
        const float expected=(root20.tracks[6]-root0.tracks[6])*fox.attr.size*facing;
        assert(std::abs(expected)>100 && std::abs(fox.position.x-expected)<.01f && fox.grounded);
    }
    sagas::FighterBody combo;combo.attr=sagas::fighter_attributes(combo.kind);
    sagas::FighterCombat::advance_jab(combo,true,false);
    assert(combo.jab_stage==1 && combo.status==sagas::FighterStatus::Attack);
    combo.action_frame=2;combo.hit_mask=2;
    sagas::FighterCombat::advance_jab(combo,true,false);
    assert(combo.jab_queued && combo.jab_stage==1);
    combo.action_frame=10;
    sagas::FighterCombat::advance_jab(combo,false,false);
    assert(combo.jab_stage==2 && combo.action_frame==0 && combo.hit_mask==0);
    combo.action_frame=8;
    sagas::FighterCombat::advance_jab(combo,true,false);
    assert(combo.jab_stage==3);
    sagas::FighterCombat::advance_jab(combo,false,true);
    assert(combo.status==sagas::FighterStatus::Wait);
    sagas::BattleCamera camera;
    sagas::Stage3D camera_stage;
    std::array<sagas::FighterBody,2> camera_fighters{};
    camera_fighters[0].position.x=-500;camera_fighters[1].position.x=500;
    for (int frame=0;frame<300;++frame) camera.tick(camera_fighters,camera_stage);
    const float close_z=camera.view().eye.z;
    camera_fighters[0].position.x=-4000;camera_fighters[1].position.x=4000;
    for (int frame=0;frame<300;++frame) camera.tick(camera_fighters,camera_stage);
    assert(camera.view().eye.z>close_z && camera.view().near_plane==256);
    // Smash input is a directional tap, not merely holding a direction.
    combo={};combo.attr=sagas::fighter_attributes(combo.kind);combo.stick_x=80;combo.tap_stick_x=0;
    assert(sagas::FighterCombat::start_smash(combo,true));
    assert(combo.attack_motion==sagas::fighter_source_data[1].smash[0]);
    sagas::FighterCombat::advance_jab(combo,false,true);
    combo.tap_stick_x=4;
    assert(!sagas::FighterCombat::start_smash(combo,true));
    combo.stick_x=0;combo.stick_y=80;combo.tap_stick_y=0;
    assert(sagas::FighterCombat::start_smash(combo,true));
    assert(combo.attack_motion==sagas::fighter_source_data[1].smash[1]);
    sagas::FighterBody rapid;rapid.kind=sagas::FighterKind::Fox;rapid.attr=sagas::fighter_attributes(rapid.kind);
    sagas::FighterCombat::advance_jab(rapid,true,false);
    for (int i=0;i<4;++i) {rapid.action_frame=2+i;sagas::FighterCombat::advance_jab(rapid,i%2==0,false,i%2!=0);}
    rapid.action_frame=10;sagas::FighterCombat::advance_jab(rapid,false,false);
    assert(rapid.jab_stage==2);
    rapid.action_frame=10;sagas::FighterCombat::advance_jab(rapid,false,false);
    assert(rapid.jab_stage==4);
    sagas::FighterCombat::advance_jab(rapid,false,true);assert(rapid.jab_stage==5);
    sagas::FighterCombat::advance_jab(rapid,true,false);
    sagas::FighterCombat::advance_jab(rapid,false,true);assert(rapid.jab_stage==5);
    sagas::FighterCombat::advance_jab(rapid,false,true);assert(rapid.jab_stage==6);
    sagas::FighterCombat::advance_jab(rapid,false,true);assert(rapid.status==sagas::FighterStatus::Wait);
    const std::array<sagas::CollisionSegment,1> ledge_floor{{{{0,0},{2000,0},0,0x8000,false,12}}};
    std::array<sagas::FighterBody,2> ledge_fighters{};
    auto& grabber=ledge_fighters[0];grabber.attr=sagas::fighter_attributes(grabber.kind);
    grabber.grounded=false;grabber.status=sagas::FighterStatus::Fall;grabber.position={-200,-370,0};
    assert(sagas::FighterPhysics::try_ledge(grabber,{-200,-350,0},ledge_floor,ledge_fighters));
    assert(grabber.status==sagas::FighterStatus::CliffCatch && grabber.cliff_edge.x==0);
    auto& second=ledge_fighters[1];second=grabber;second.status=sagas::FighterStatus::Fall;second.position={-200,-370,0};
    assert(!sagas::FighterPhysics::try_ledge(second,{-200,-350,0},ledge_floor,ledge_fighters));
    second.lr=-1;
    assert(!sagas::FighterPhysics::try_ledge(second,{-200,-350,0},ledge_floor,{}));
    for (unsigned kind=0;kind<12;++kind) {
        const auto& data=sagas::fighter_source_data[kind];
        for (auto clip:data.cliff) {
            const auto actor=scene_loader.fighter_motion(static_cast<sagas::FighterKind>(kind),clip,0x40000000);
            assert(actor.fighter_root_animation);
            for (const auto joint:actor.source_joint_ids) {
                const auto point=pose_renderer.joint_point(actor,10,joint,{});
                assert(std::isfinite(point.x) && std::isfinite(point.y));
            }
        }
    }
    std::cout << "Sagas core tests passed\n";
}
