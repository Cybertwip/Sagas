#include <sagas/Engine.hpp>
#include <sagas/N64.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

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
        services.audio.play_music("audio/opening.sgpcm", 1.0f);
        resources_->prefetch("room.action");
    }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        // Look-ahead loading is aligned with quiet portions of the source
        // timeline, leaving the render loop free of relocation/model stalls.
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
        if (tic_ == 2000) services.resources.prefetch("cliff");
        if (tic_ == 2160) {
            services.resources.activate("cliff");
            services.resources.prefetch("yamabuki");
        }
        if (tic_ == 2320) {
            services.resources.activate("yamabuki");
            services.resources.prefetch("yoster");
        }
        if (tic_ == 2330) services.resources.release("cliff");
        if (tic_ == 2490) services.resources.release("yamabuki");
        if (tic_ == 2800) services.resources.activate("yoster");
        if (tic_ == 2840) services.resources.prefetch("sector");
        if (tic_ == 2960) {
            services.resources.activate("sector");
            services.resources.prefetch("standoff");
        }
        if (tic_ == 2970) services.resources.release("yoster");
        if (tic_ == 3120) services.resources.activate("standoff");
        if (tic_ == 3130) services.resources.release("sector");
        if (tic_ == 3450) services.resources.release("standoff");
        if (tic_ >= 10 && (input.accept_pressed || input.cancel_pressed || input.skip_pressed)) done_ = true;
        if (tic_ >= total_duration) done_ = true;
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0, 0, 0, 255});
        renderer_->begin();
        const auto [kind, local] = locate(tic_);
        switch (kind) {
            case Segment::Room: room(r, local); break;
            case Segment::Portraits: portraits(r, local); break;
            case Segment::Mario: fighter(r, local, "MVOpeningPortraitsSet1/Mario.png", {164, 42, 36, 255}); break;
            case Segment::Donkey: fighter(r, local, "MVOpeningPortraitsSet2/Donkey.png", {91, 52, 31, 255}); break;
            case Segment::Link: fighter(r, local, "MVOpeningPortraitsSet2/Link.png", {34, 80, 44, 255}); break;
            case Segment::Samus: fighter(r, local, "MVOpeningPortraitsSet1/Samus.png", {116, 63, 31, 255}); break;
            case Segment::Yoshi: fighter(r, local, "MVOpeningPortraitsSet2/Yoshi.png", {36, 105, 49, 255}); break;
            case Segment::Kirby: fighter(r, local, "MVOpeningPortraitsSet2/Kirby.png", {141, 76, 92, 255}); break;
            case Segment::Fox: fighter(r, local, "MVOpeningPortraitsSet1/Fox.png", {74, 77, 94, 255}); break;
            case Segment::Pikachu: fighter(r, local, "MVOpeningPortraitsSet1/Pikachu.png", {139, 113, 32, 255}); break;
            case Segment::Run: wallpaper(r, "MVOpeningRun/Wallpaper.png", {2,2}); break;
            case Segment::Cliff: {
                wallpaper(r, "MVOpeningStandoffWallpaper.png", {2,2});
                const auto camera = loader_->camera("llMVOpeningCliffCamAnimJoint", local);
                renderer_->draw(r, model("cliff.hills"), camera, local);
                renderer_->draw(r, model("cliff.ocarina"), camera, local);
                break;
            }
            case Segment::Yamabuki: {
                wallpaper(r, "MVOpeningYamabuki/Wallpaper.png");
                const auto camera = loader_->camera("llMVOpeningYamabukiCamAnimJoint", local);
                renderer_->draw(r, model("yamabuki.shadow"), camera, local, {45,45,55,120});
                renderer_->draw(r, model("yamabuki.legs"), camera, local);
                renderer_->draw(r, model("yamabuki.ball"), camera, local);
                break;
            }
            case Segment::Jungle: fighter(r, local, "MVOpeningPortraitsSet2/Donkey.png", {22, 67, 32, 255}); break;
            case Segment::Yoster: {
                wallpaper(r, "StageYoshi.png");
                const auto camera = loader_->camera("llMVOpeningYosterCamAnimJoint", local);
                renderer_->draw(r, model("yoster.nest"), camera, local);
                renderer_->draw(r, model("yoster.ground"), camera, local);
                break;
            }
            case Segment::Sector: {
                wallpaper(r, "MVOpeningSectorWallpaper.png");
                renderer_->draw(r, model("sector.great_fox"),
                                loader_->camera("llMVOpeningSectorCamAnimJoint", local), local);
                renderer_->flush(r);
                r.sprite("textures/MVOpeningSector/Cockpit.png", {160,120});
                break;
            }
            case Segment::Standoff: {
                wallpaper(r, "MVOpeningStandoffWallpaper.png", {2,2});
                const auto camera = loader_->camera("llMVOpeningStandoffCamAnimJoint", local);
                renderer_->draw(r, model("standoff.ground"), camera, local);
                renderer_->draw(r, model("standoff.lightning"), camera, local);
                break;
            }
            case Segment::Clash: clash(r, local); break;
            case Segment::Newcomers: newcomers(r, local); break;
        }
        renderer_->end(r);
        // Original opening scenes all fade through black at their boundaries.
        const auto duration = durations[static_cast<std::size_t>(kind)];
        const float edge = std::min({1.0f, local / 10.0f, (duration - local) / 10.0f});
        if (edge < 1) r.fill(0, 0, 320, 240, {0,0,0,static_cast<std::uint8_t>((1-edge)*255)});
        r.end();
    }
    std::unique_ptr<Scene> next() override { return done_ ? make_title_scene() : nullptr; }
private:
    enum class Segment : std::size_t {
        Room, Portraits, Mario, Donkey, Link, Samus, Yoshi, Kirby, Fox, Pikachu,
        Run, Cliff, Yamabuki, Jungle, Yoster, Sector, Standoff, Clash, Newcomers
    };
    static constexpr std::array<int, 19> durations{
        1320, 150, 60, 60, 60, 60, 60, 60, 60, 60, 220, 160, 160, 320, 160, 160, 320, 160, 40};
    static constexpr int total_duration = 3650;

    static std::pair<Segment, int> locate(int tic) {
        int start{};
        for (std::size_t i = 0; i < durations.size(); ++i) {
            if (tic < start + durations[i]) return {static_cast<Segment>(i), tic - start};
            start += durations[i];
        }
        return {Segment::Newcomers, durations.back() - 1};
    }
    static void wallpaper(RenderEngine& r, std::string_view name, Vec2 scale = {1,1}) {
        r.sprite(std::string("textures/") + std::string(name), {160,120}, scale);
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

        LightingRig warm_room;
        warm_room.ambient = {184,170,158,255};
        warm_room.ambient_intensity = 0.64f;
        warm_room.key = {{-0.28f,0.78f,0.56f},{255,236,204,255},0.56f};
        warm_room.reflection={255,226,194,255};
        warm_room.reflection_intensity=0.22f;
        warm_room.shininess=8.0f;
        if (local < 1040) {
            renderer_->draw(r, model("room.outside"), camera, camera_frame, {210,226,255,255}, warm_room);
            renderer_->draw(r, model("room.haze"), camera, camera_frame, {220,225,235,150}, warm_room);
            renderer_->draw(r, model("room.background"), camera, static_cast<float>(local),
                            {255,255,255,255}, warm_room);
            if (local < 450) renderer_->draw(r, model("room.sunlight"), camera, camera_frame,
                                             {255,240,190,150}, warm_room);
            renderer_->draw(r, model("room.desk"), camera, camera_frame, {255,255,255,255}, warm_room);
            const float prop_frame = static_cast<float>(std::max(local - 560, 0));
            renderer_->draw(r, model("room.books"), camera, prop_frame, {255,255,255,255}, warm_room);
            if (local >= 280) renderer_->draw(r, model("room.pencils"), camera, prop_frame,
                                              {255,255,255,255}, warm_room);
            renderer_->draw(r, model("room.lamp"), camera, prop_frame, {255,255,255,255}, warm_room);
            renderer_->draw(r, model("room.tissues"), camera, prop_frame, {255,255,255,255}, warm_room);
            const Model3D& boss = local < 560 ? model("boss.pose1") :
                                  (local < 860 ? model("boss.pose2") : model("boss.pose3"));
            const float boss_frame = static_cast<float>(local < 560 ? local : (local < 860 ? local-560 : local-860));
            const auto draw_pulled_fighter = [&] {
                if (local < 380) {
                    const float pickup_frame=static_cast<float>(local-280);
                    // Runtime joint 5 (item-heavy) is descriptor node 1;
                    // the four special fighter joints precede this tree.
                    const auto held=renderer_->placed_at_joint(model("mario.pickup"),pickup_frame,boss,boss_frame,1);
                    renderer_->draw(r,held,camera,pickup_frame,{255,255,255,255},warm_room);
                } else {
                    auto falling=model("mario.fall");
                    const auto release=renderer_->placed_at_joint(model("mario.pickup"),100.0f,
                                                                   model("boss.pose1"),380.0f,1);
                    if (release.root_transform) {
                        falling.position={(*release.root_transform)[3],(*release.root_transform)[7],
                                          (*release.root_transform)[11]};
                    }
                    renderer_->draw(r,falling,camera,static_cast<float>(local-380),{255,255,255,255},warm_room);
                }
            };
            // Static props and animated fighters share the native GPU depth
            // buffer, regardless of whether their source mesh was skinned.
            if (local >= 280 && local < 500) draw_pulled_fighter();
            if (local >= 695) renderer_->draw(r,model("link.fall"),camera,static_cast<float>(local-695),
                                              {255,255,255,255},warm_room);
            if (local < 280) renderer_->draw(r,model("room.boss_shadow"),camera,static_cast<float>(local),
                                             {90,80,78,150},warm_room);
            renderer_->draw(r,boss,camera,boss_frame,{255,255,255,255},warm_room);
            if (local >= 500) draw_pulled_fighter();
            if (local >= 500) renderer_->draw(r,model("room.spotlight"),camera,static_cast<float>(local-500),
                                              {255,244,210,105},warm_room);
            if (local >= 860) renderer_->draw(r,model("room.snap"),camera,static_cast<float>(local-860),
                                              {255,255,255,255},warm_room);
            if (local < 280) renderer_->draw(r,model("room.logo"),camera,static_cast<float>(local),
                                             {255,255,255,255},warm_room);
        } else {
            wallpaper(r, "MVOpeningRoomWallpaper.png");
            renderer_->draw(r, model("room.desk_ground"), camera,
                            static_cast<float>(std::max(local - 1060, 0)),
                            {255,255,255,255}, warm_room);
            if (local < 1140) {
                Camera3D transition_camera{{0,0,1000},{0,0,0},{0,1,0},39.56115341f,128,16384};
                renderer_->draw(r,model("room.transition_outline"),transition_camera,
                                static_cast<float>(local-1040),
                                {255,255,255,255},warm_room);
                renderer_->draw(r,model("room.transition_overlay"),transition_camera,
                                static_cast<float>(local-1040),
                                {255,255,255,190},warm_room);
            } else {
                const float closeup_frame=static_cast<float>(local-1140);
                renderer_->draw(r,model("room.closeup_ground"),camera,closeup_frame,
                                {255,255,255,255},warm_room);
                renderer_->draw(r,model("room.closeup_air"),camera,closeup_frame,
                                {255,255,255,210},warm_room);
                auto revival=model("mario.revival");
                const auto release=renderer_->placed_at_joint(model("mario.pickup"),100.0f,
                                                               model("boss.pose1"),380.0f,1);
                if (release.root_transform)
                    revival.position={(*release.root_transform)[3],(*release.root_transform)[7],
                                      (*release.root_transform)[11]};
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
    static void fighter(RenderEngine& r, int local, std::string_view portrait, Color background) {
        r.fill(10, 10, 300, 220, background);
        const float scale = 1.0f + 0.1f * local / 60.0f;
        r.sprite(std::string("textures/") + std::string(portrait), {160,120}, {scale, 4.0f});
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
    int tic_{};
    bool done_{};
    SceneResourceManager* resources_{};
    Scene3DLoader* loader_{};
    std::unique_ptr<Scene3DRenderer> renderer_;
};

} // namespace

std::unique_ptr<Scene> make_opening_scene() { return std::make_unique<OpeningScene>(); }

} // namespace sagas
