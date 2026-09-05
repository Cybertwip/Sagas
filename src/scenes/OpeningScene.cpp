#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/N64.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace sagas {
namespace {

class OpeningScene final : public Scene {
public:
    void enter(Services& services) override {
        resources_ = &services.resources;
        resources_->load_manifest("scenes/opening.sgscene");
        resources_->activate("room.base");
        loader_ = &resources_->loader();
        renderer_ = std::make_unique<Scene3DRenderer>(resources_->archive());
        for (const auto& segment : resources_->timeline()) total_duration_ += segment.duration;
        services.audio.play_music("audio/opening.sgpcm", 1.0f);
        resources_->prefetch("room.action");
    }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        // The room has source-scripted sub-scenes inside one timeline segment.
        if (tic_ == 250) services.resources.activate("room.action");
        if (tic_ == 900) services.resources.prefetch("room.transition");
        if (tic_ == 1030) {
            services.resources.activate("room.transition");
            services.resources.prefetch("room.closeup");
        }
        if (tic_ == 1120) services.resources.activate("room.closeup");
        if (tic_ == 1320) {
            services.resources.release("room.base");
            services.resources.release("room.action");
            services.resources.release("room.transition");
            services.resources.release("room.closeup");
        }

        std::uint32_t start{};
        for (const auto& segment : resources_->timeline()) {
            if (!segment.bundle.empty()) {
                if (segment.preload_lead <= start &&
                    tic_ == static_cast<int>(start - segment.preload_lead))
                    services.resources.prefetch(segment.bundle);
                if (tic_ == static_cast<int>(start)) services.resources.activate(segment.bundle);
                if (tic_ == static_cast<int>(start + segment.duration))
                    services.resources.release(segment.bundle);
            }
            start += segment.duration;
        }
        if (tic_ >= 10 && (input.accept_pressed || input.cancel_pressed || input.skip_pressed)) done_ = true;
        if (tic_ >= total_duration_) done_ = true;
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0, 0, 0, 255});
        renderer_->begin();
        const auto position = locate(tic_);
        const auto& segment = *position.segment;
        const int local = position.local;
        if (segment.renderer == "room") {
            room(r, local);
        } else if (segment.renderer == "portraits") {
            portraits(r, local);
        } else if (segment.renderer == "fighter_intro") {
            fighter_intro(r,local,segment.name);
        } else if (segment.renderer == "fighter") {
            fighter(r, local, segment.argument, segment.color);
        } else if (segment.renderer == "wallpaper") {
            wallpaper(r, segment.argument, segment.scale);
        } else if (segment.renderer == "models" || segment.renderer == "models_cockpit") {
            wallpaper(r, segment.argument, segment.scale);
            for (const auto& cue : segment.cues) {
                const auto camera = loader_->camera(cue.camera, static_cast<float>(local));
                renderer_->draw(r, model(cue.resource), camera, static_cast<float>(local), cue.tint);
            }
            if (segment.renderer == "models_cockpit") {
                renderer_->flush(r);
                r.sprite("textures/MVOpeningSector/Cockpit.png", {160,120});
            }
        } else if (segment.renderer == "clash") {
            clash(r, local);
        } else if (segment.renderer == "newcomers") {
            newcomers(r, local);
        } else {
            throw std::runtime_error("unknown opening render strategy: " + segment.renderer);
        }
        renderer_->end(r);
        if (segment.renderer=="room") {
            // Original room cameras use the 10,10–310,230 viewport.
            r.fill(0,0,320,10,{0,0,0,255});
            r.fill(0,230,320,10,{0,0,0,255});
            r.fill(0,10,10,220,{0,0,0,255});
            r.fill(310,10,10,220,{0,0,0,255});
        }
        const float edge = (segment.renderer == "room" || segment.renderer == "fighter_intro") ? 1.0f : std::min({1.0f, local / 10.0f,
                                     (static_cast<int>(segment.duration) - local) / 10.0f});
        if (edge < 1) r.fill(0, 0, 320, 240,
                             {0,0,0,static_cast<std::uint8_t>((1-edge)*255)});
        r.end();
    }
    std::unique_ptr<Scene> next() override { return done_ ? make_title_scene() : nullptr; }
private:
    struct TimelinePosition { const SceneTimelineSegment* segment{}; int local{}; };
    [[nodiscard]] TimelinePosition locate(int tic) const {
        int start{};
        for (const auto& segment : resources_->timeline()) {
            if (tic < start + static_cast<int>(segment.duration)) return {&segment, tic - start};
            start += segment.duration;
        }
        const auto& last = resources_->timeline().back();
        return {&last, static_cast<int>(last.duration) - 1};
    }
    static void wallpaper(RenderEngine& r, std::string_view name, Vec2 scale = {1,1}) {
        r.sprite(std::string("textures/") + std::string(name), {160,120}, scale);
    }
    void draw_room_shell(RenderEngine& r, const Camera3D& camera, float camera_frame,
                         int local, const LightingRig& lights) {
        renderer_->draw(r,model("room.outside"),camera,camera_frame,{255,255,255,255},lights);
        // The original DL link and combiner provide atmosphere opacity.
        renderer_->draw(r,model("room.haze"),camera,camera_frame,{255,255,255,255},lights);
        renderer_->draw(r,model("room.background"),camera,static_cast<float>(local),
                        {255,255,255,255},lights);
        if (false)
            renderer_->draw(r,model("room.sunlight"),camera,camera_frame,
                            {255,255,255,255},lights);
        renderer_->draw(r,model("room.desk"),camera,camera_frame,{255,255,255,255},lights);
        const float prop_frame=static_cast<float>(std::max(local-560,0));
        renderer_->draw(r,model("room.books"),camera,prop_frame,{255,255,255,255},lights);
        renderer_->draw(r,model("room.lamp"),camera,prop_frame,{255,255,255,255},lights);
        renderer_->draw(r,model("room.tissues"),camera,prop_frame,{255,255,255,255},lights);
        if (local < 280)
            renderer_->draw(r,model("room.boss_shadow"),camera,static_cast<float>(local),
                            {255,255,255,150},lights);
    }
    [[nodiscard]] const Model3D& room_boss(int local) const {
        return local < 560 ? model("boss.pose1")
             : local < 860 ? model("boss.pose2") : model("boss.pose3");
    }
    [[nodiscard]] Model3D dropped_mario() {
        auto falling=model("mario.fall");
        // FTAttributes.joint_itemheavy_id is 5 for Master Hand. The common
        // descriptor starts at joint 4, so its holding joint is node 1.
        // Status changes run before the next fighter update: retain the
        // attachment from the last pulled frame (room tic 379).
        const auto release=renderer_->placed_at_joint(model("mario.pickup"),99.0f,
                                                       model("boss.pose1"),379.0f,1);
        if (release.root_transform)
            falling.position={(*release.root_transform)[3],(*release.root_transform)[7],
                              (*release.root_transform)[11]};
        falling.rotation={};
        return falling;
    }
    void draw_room_cast(RenderEngine& r, const Camera3D& camera, int local,
                        const LightingRig& lights) {
        const auto& boss=room_boss(local);
        const float boss_frame=static_cast<float>(local < 560 ? local
                                      : local < 860 ? local-560 : local-860);
        constexpr std::size_t held_joint=1;
        renderer_->draw(r,boss,camera,boss_frame,{255,255,255,255},lights);
        if (local >= 280) {
            const float prop_frame=static_cast<float>(std::max(local-560,0));
            renderer_->draw(r,model("room.pencils"),camera,prop_frame,
                            {255,255,255,255},lights);
            if (local < 380) {
                const float pickup_frame=static_cast<float>(local-280);
                const auto held=renderer_->placed_at_joint(model("mario.pickup"),pickup_frame,
                                                            boss,boss_frame,held_joint);
                renderer_->draw(r,held,camera,pickup_frame,{255,255,255,255},lights);
            } else if (local < 500) {
                const auto falling=dropped_mario();
                renderer_->draw(r,falling,camera,static_cast<float>(local-380),
                                {255,255,255,255},lights);
            }
        }
        if (local >= 695) {
            auto link=model("link.fall");
            renderer_->draw(r,link,camera,static_cast<float>(local-695),
                            {255,255,255,255},lights);
        }
    }
    void room(RenderEngine& r, int local) {
        // These are the four original camera programs and scene changes from
        // mvOpeningRoomFuncRun.  The wallpaper only exists after tic 1040.
        Camera3D camera;
        float camera_frame{};
        if (local < 560) {
            camera.near_plane = 80;
            camera.far_plane = 15000;
            camera_frame = static_cast<float>(local);
            camera = loader_->camera("llMVOpeningRoomScene1CamAnimJoint", camera_frame, camera);
        } else if (local < 860) {
            camera_frame = static_cast<float>(local - 560);
            camera = loader_->camera("llMVOpeningRoomScene2CamAnimJoint", camera_frame, camera);
        } else if (local < 1140) {
            camera = {{9.2993f,3880.3894f,4077.9817f}, {0.991579f,2995.6814f,-388.95343f},
                      {0,1,0}, 18.607187f,128,16384};
            camera_frame = static_cast<float>(local - 860);
            camera = loader_->camera("llMVOpeningRoomScene3CamAnimJoint", camera_frame, camera);
        } else {
            camera = {{-1039.8806f,3199.2156f,-1235.1688f}, {-1162.4098f,2127.8245f,-3853.0732f},
                      {0,1,0}, 11.982265f,128,16384};
            camera_frame = static_cast<float>(local - 1140);
            camera = loader_->camera("llMVOpeningRoomScene4CamAnimJoint", camera_frame, camera);
        }

        LightingRig warm_room = LightingSystem::opening_room_at(local);
        if (local >= 500 && local < 1040) {
            const auto& halo = model("room.spotlight");
            if (halo.emit_spotlight)
                LightingSystem::aim_opening_spotlight(
                    warm_room, halo.position, std::clamp((local-500)/18.0f,0.0f,1.0f));
        }
        if (local < 1040) {
            // Remix constructs the complete room and Master Hand before its
            // logo blackout.  Drawing only the logo here caused the camera
            // reveal to remain literally black until tic 280.
            draw_room_shell(r,camera,camera_frame,local,warm_room);
            draw_room_cast(r,camera,local,warm_room);
            if (local < 280) {
                renderer_->flush(r);
                const int overlay=local < 60 ? 255 : std::max(0,255-(local-59)*13);
                if (overlay>0)
                    r.fill(10,10,300,220,{0,0,0,static_cast<std::uint8_t>(overlay)});
                r.clear_depth();
                renderer_->draw(r,model("room.logo"),camera,static_cast<float>(local),
                                {255,255,255,255},warm_room);
            }
            if (local >= 450) {
                // DL link 26 overlays the room; at 500 the pulled fighter
                // moves to link 9, which is rendered after this overlay.
                renderer_->flush(r);
                const int alpha=std::min(160,(local-449)*9);
                r.fill(10,10,300,220,{0,0,0,static_cast<std::uint8_t>(alpha)});
                if (local >= 500) {
                    renderer_->draw(r,model("room.spotlight"),camera,static_cast<float>(local-500),
                                    {255,255,255,255},warm_room);
                    renderer_->draw(r,dropped_mario(),camera,static_cast<float>(local-380),
                                    {255,255,255,255},warm_room);
                }
                if (local >= 860)
                    renderer_->draw(r,model("room.snap"),camera,static_cast<float>(local-860),
                                    {255,255,255,255},warm_room);
            }
        } else {
            wallpaper(r, "MVOpeningRoomWallpaper.png");
            renderer_->draw(r, model("room.desk_ground"), camera,
                            static_cast<float>(std::max(local - 1060, 0)),
                            {255,255,255,255}, warm_room);
            // Remix points both transition display routines at the N64 depth
            // buffer. They are masks for the captured wallpaper, never black
            // color geometry. The wallpaper above is already the composited
            // result, so drawing either mesh here exposes the outline as the
            // large black "missing floor" polygon.
            if (local < 1140) {
                renderer_->draw(r,dropped_mario(),camera,static_cast<float>(local-380),
                                {255,255,255,255},warm_room);
            }
            if (local >= 1140) {
                const float closeup_frame=static_cast<float>(local-1140);
                renderer_->draw(r,model("room.closeup_ground"),camera,closeup_frame,
                                {255,255,255,255},warm_room);
                renderer_->draw(r,model("room.closeup_air"),camera,closeup_frame,
                                {255,255,255,210},warm_room);
                auto revival=model("mario.revival");
                revival.position=renderer_->fighter_position(dropped_mario(),759.0f);
                renderer_->draw(r,revival,camera,closeup_frame,{255,255,255,255},warm_room);
            }
        }
    }
    static void portraits(RenderEngine& r, int local) {
        static constexpr std::array<std::string_view, 4> set1{"Samus", "Mario", "Fox", "Pikachu"};
        static constexpr std::array<std::string_view, 4> set2{"Link", "Kirby", "Donkey", "Yoshi"};
        const auto& set = local < 75 ? set1 : set2;
        for (std::size_t i = 0; i < set.size(); ++i) {
            const float phase = std::clamp((local % 75 - static_cast<int>(i) * 15) / 8.0f, 0.0f, 1.0f);
            const float x = local < 75 ? 160.0f - (1-phase)*320.0f : 160.0f + (1-phase)*320.0f;
            r.sprite("textures/MVOpeningPortraitsSet" + std::to_string(local < 75 ? 1 : 2) + "/" + std::string(set[i]) + ".png",
                     {x, 37.5f + static_cast<float>(i) * 55.0f});
        }
    }
    void fighter_intro(RenderEngine& r,int local,std::string_view name) {
        struct Intro {
            std::string_view key, fighter_name, letters;
            std::array<float,7> letter_x;
            float name_x;
            std::array<float,4> viewport;
            Vec3 start;
            Color background;
        };
        // mvOpening{Fighter}MakeName / MakePosedFighter / MakePosedFighterCamera.
        static constexpr Intro intros[]{
            {"mario","Mario","MARIO",{0,40,80,110,125},80,{10,10,100,220},{0,600,0},{160,170,255,255}},
            {"donkey","Donkey","DK",{0,40},120,{210,10,100,220},{0,-600,0},{70,90,0,255}},
            {"link","Link","LINK",{0,30,45,80},100,{10,10,300,80},{600,0,0},{150,120,180,255}},
            {"samus","Samus","SAMUS",{0,30,70,110,140},80,{10,10,100,220},{0,600,0},{0,0,80,255}},
            {"yoshi","Yoshi","YOSHI",{0,30,65,95,128},80,{10,150,300,80},{-600,0,0},{255,190,90,255}},
            {"kirby","Kirby","KIRBY",{0,35,50,80,110},90,{210,10,100,220},{0,600,0},{80,170,255,255}},
            {"fox","Fox","FOX",{0,30,75},110,{210,10,100,220},{0,600,0},{0,60,40,255}},
            {"pikachu","Pikachu","PIKACHU",{0,30,45,75,110,140,170},65,{10,10,100,220},{0,-600,0},{110,170,110,255}}
        };
        const auto found=std::find_if(std::begin(intros),std::end(intros),
                                      [name](const auto& intro) { return intro.key==name; });
        if (found==std::end(intros)) throw std::runtime_error("unknown fighter introduction");
        const auto& intro=*found;
        if (local<15) {
            for (std::size_t i=0;i<intro.letters.size();++i)
                r.sprite_at(std::string("textures/IFCommonAnnounceCommon/Letter")+intro.letters[i]+".png",
                            {intro.name_x+intro.letter_x[i],100});
            return;
        }
        draw_intro_motion(r,local,name);
        const auto& vp=intro.viewport;
        r.fill(vp[0],vp[1],vp[2],vp[3],intro.background);
        auto posed=model(std::string("intro.")+std::string(name)+".stance");
        float distance{},speed{};
        for (int tick=15;tick<=local && tick<60;++tick) {
            if (tick==15) speed=17;
            if (tick==45) speed=15;
            if (tick>15 && tick<45) speed-=1.0f/15.0f;
            if (tick>45 && tick<60) speed-=1;
            distance+=speed;
        }
        const float remaining=1.0f-distance/600.0f;
        posed.position={intro.start.x*remaining,intro.start.y*remaining,0};
        Camera3D initial;
        initial.viewport=vp;
        initial.aspect=vp[2]/vp[3];
        const auto camera=loader_->camera(std::string("llMVOpeningCommon")+
            std::string(intro.fighter_name)+"CamAnimJoint",static_cast<float>(local),initial);
        renderer_->draw(r,posed,camera,static_cast<float>(local-15),{255,255,255,255},
                        LightingSystem::opening_room());
    }
    void draw_intro_motion(RenderEngine& r,int local,std::string_view name) {
        struct MotionView {
            std::string_view key,stage,wallpaper;
            std::array<float,4> viewport;
            Vec3 eye0,at0,eye1,at1;
            float roll0,roll1;
            std::uint32_t motion;
        };
        // CObjDesc start/end and MoviePlayer1 map objects from mvOpening*.c.
        static constexpr MotionView views[]{
            {"mario","Castle","MVOpeningRoomWallpaper",{110,10,200,220},{300,500,1700},{0,100,0},{800,500,1300},{100,100,0},.15f,.15f,606},
            {"donkey","Jungle","StageJungle",{10,10,200,220},{-1100,150,400},{0,150,0},{-900,500,1800},{0,500,0},0,0,942},
            {"link","Hyrule","StageCastle",{10,90,300,140},{-800,180,800},{0,180,0},{200,0,400},{0,240,0},0,.4f,1188},
            {"samus","Zebes","StageZebes",{110,10,200,220},{400,1100,0},{0,200,0},{1600,230,200},{0,200,0},.6f,.6f,1015},
            {"yoshi","Yoster","StageYoshi",{10,10,300,140},{1200,150,1000},{100,200,0},{2000,100,600},{1300,100,-100},0,0,1821},
            {"kirby","Pupupu","StageDreamLand",{10,10,200,220},{0,400,2000},{0,400,0},{1100,400,1800},{1100,400,0},0,0,1269},
            {"fox","Sector","StageSector",{10,10,200,220},{-400,320,100},{0,320,0},{-3000,300,250},{0,300,-200},0,.7f,779},
            {"pikachu","Yamabuki","StagePokemon",{110,10,200,220},{0,0,20000},{0,0,0},{50,-1640,1000},{50,-1640,0},0,0,1957}
        };
        const auto& view=*std::find_if(std::begin(views),std::end(views),
            [name](const auto& v) { return v.key==name; });
        if (motion_stage_name_!=name) {
            motion_stage_=loader_->stage("llGR"+std::string(view.stage)+"MapMapHeader");
            motion_stage_name_=name;
        }
        const auto& vp=view.viewport;
        r.scissor_game(vp[0],vp[1],vp[2],vp[3]);
        if (!view.wallpaper.empty()) r.sprite("textures/"+std::string(view.wallpaper)+".png",
                 {vp[0]+vp[2]/2,vp[1]+vp[3]/2},{vp[2]/320,vp[3]/240});
        r.reset_scissor();
        Camera3D camera;
        camera.viewport=vp;
        camera.aspect=vp[2]/vp[3];
        const float time=static_cast<float>(local-15);
        const float fraction=time/45.0f;
        const auto interpolate=[&](Vec3 a,Vec3 b) {
            return Vec3{a.x+(b.x-a.x)*fraction+motion_stage_.movie_player1.x,
                        a.y+(b.y-a.y)*fraction+motion_stage_.movie_player1.y,
                        a.z+(b.z-a.z)*fraction};
        };
        camera.eye=interpolate(view.eye0,view.eye1);
        camera.at=interpolate(view.at0,view.at1);
        camera.up.x=view.roll0+(view.roll1-view.roll0)*fraction;
        for (const auto& layer:motion_stage_.layers)
            renderer_->draw(r,layer,camera,time,{255,255,255,255},LightingSystem::opening_room());
        auto fighter=model("intro."+std::string(name)+".stance");
        fighter.position=motion_stage_.movie_player1;
        fighter.rotation.y=std::numbers::pi_v<float>/2;
        std::uint32_t motion=view.motion;
        float frame=time;
        // Recorded button events select the actual cartridge clips. Full
        // fighter status/physics playback is tracked in OPENING_TASKS.md.
        if (name=="mario") {
            if (time>=33) { motion=614; frame=time-33; }
            else if (time>=12) { motion=607; frame=time-12; }
        } else if (name=="donkey" && time>=4) { motion=943; frame=time-4; }
        else if (name=="fox") { fighter.rotation.y=-std::numbers::pi_v<float>/2; frame=std::fmod(time,13.0f); }
        else if (name=="yoshi" && time>=21) { motion=1876; frame=time-21; }
        else if (name=="kirby" && time>=4) { motion=1387; frame=time-4; }
        n64::AnimationDecoder decoder(resources_->archive());
        fighter.animation=decoder.table({motion,0},fighter.nodes.size());
        renderer_->draw(r,fighter,camera,frame,{255,255,255,255},LightingSystem::opening_room());
        renderer_->flush(r);
    }
    void fighter(RenderEngine& r, int local, std::string_view portrait, Color background) {
        r.fill(10, 10, 300, 220, background);
        FighterKind kind=FighterKind::Mario;
        bool has_fighter=true;
        GeometryLayout layout = GeometryLayout::Direct;
        if (portrait.find("Mario") != std::string_view::npos) kind=FighterKind::Mario;
        else if (portrait.find("Donkey") != std::string_view::npos) kind=FighterKind::Donkey;
        else if (portrait.find("Link") != std::string_view::npos) kind=FighterKind::Link;
        else if (portrait.find("Samus") != std::string_view::npos) kind=FighterKind::Samus;
        else if (portrait.find("Yoshi") != std::string_view::npos) kind=FighterKind::Yoshi;
        else if (portrait.find("Kirby") != std::string_view::npos) kind=FighterKind::Kirby;
        else if (portrait.find("Fox") != std::string_view::npos) kind=FighterKind::Fox;
        else if (portrait.find("Pikachu") != std::string_view::npos) kind=FighterKind::Pikachu;
        else has_fighter=false;
        if (has_fighter && loader_) {
            try {
                const auto spec=fighter_model_spec(kind);
                layout=spec.joint_pairs ? GeometryLayout::JointPairs : GeometryLayout::Direct;
                auto model3d = loader_->fighter_model(spec.descriptor, layout, spec.setup_parts);
                n64::AnimationDecoder decoder(resources_->archive());
                const std::uint32_t motion = portrait.find("Mario") != std::string_view::npos ? 362U :
                    (portrait.find("Link") != std::string_view::npos ? 409U : 0U);
                if (motion) {
                    auto scripts = decoder.table({motion, 0}, model3d.nodes.size() + 1);
                    if (!scripts.empty()) {
                        model3d.fighter_root.scale = {1,1,1};
                        model3d.fighter_root_animation = scripts.front();
                        model3d.animation.assign(scripts.begin() + 1, scripts.end());
                        model3d.fighter_animation = true;
                    }
                }
                float posed_y = 600.0f;
                float speed = 0;
                for (int tic = 15; tic <= local && tic < 60; ++tic) {
                    if (tic == 15) speed = 17;
                    if (tic == 45) speed = 15;
                    if (tic > 15 && tic < 45) speed -= 1.0f / 15.0f;
                    if (tic > 45 && tic < 60) speed -= 1.0f;
                    posed_y -= speed;
                }
                model3d.position = {0, posed_y, 0};
                Camera3D camera{{300,500,1700},{0,100,0},{0,1,0},18.0f,16,16384};
                renderer_->draw(r, model3d, camera, static_cast<float>(local),
                                {255,255,255,255}, LightingSystem::opening_room_at(1040));
            } catch (const std::exception&) {
                has_fighter=false;
            }
        }
        if (!has_fighter) {
            const float scale = 1.0f + 0.1f * local / 60.0f;
            r.sprite(std::string("textures/") + std::string(portrait), {160,120}, {scale, 4.0f});
        }
        r.fill(10, 10, 300, 45, {0,0,0,120});
        r.fill(10, 185, 300, 45, {0,0,0,120});
    }
    static void clash(RenderEngine& r, int local) {
        static constexpr std::array<std::string_view, 8> names{
            "MVOpeningPortraitsSet1/Mario.png", "MVOpeningPortraitsSet2/Donkey.png",
            "MVOpeningPortraitsSet2/Link.png", "MVOpeningPortraitsSet1/Samus.png",
            "MVOpeningPortraitsSet2/Yoshi.png", "MVOpeningPortraitsSet2/Kirby.png",
            "MVOpeningPortraitsSet1/Fox.png", "MVOpeningPortraitsSet1/Pikachu.png"};
        wallpaper(r, "MVOpeningStandoffWallpaper.png", {2,2});
        const auto index = static_cast<std::size_t>(local / 20) % names.size();
        r.sprite(std::string("textures/") + std::string(names[index]), {160,120}, {1,3});
        if ((local % 20) < 3) r.fill(0,0,320,240,{255,255,255,220});
    }
    static void newcomers(RenderEngine& r, int local) {
        const std::array<std::string_view, 4> names{"Link", "Kirby", "Donkey", "Yoshi"};
        for (std::size_t i = 0; i < names.size(); ++i)
            r.sprite("textures/MVOpeningPortraitsSet2/" + std::string(names[i]) + ".png",
                     {160, 37.5f + static_cast<float>(i)*55});
        if (local < 8) r.fill(0,0,320,240,{255,255,255,static_cast<std::uint8_t>((8-local)*28)});
    }
    [[nodiscard]] const Model3D& model(std::string_view key) const {
        return resources_->model(key);
    }
    Stage3D motion_stage_;
    std::string motion_stage_name_;
    int tic_{};
    int total_duration_{};
    bool done_{};
    SceneResourceManager* resources_{};
    Scene3DLoader* loader_{};
    std::unique_ptr<Scene3DRenderer> renderer_;
};

} // namespace

std::unique_ptr<Scene> make_opening_scene() { return std::make_unique<OpeningScene>(); }

} // namespace sagas
