#include <sagas/Engine.hpp>
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
        const float edge = std::min({1.0f, local / 10.0f,
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
        // mvOpeningRoom configures the global reflector at 45 degrees on
        // both axes: (sin(45)cos(45), sin(45), cos(45)cos(45)).
        warm_room.key = {{0.5f,0.70710678f,0.5f},{255,236,204,255},0.62f};
        warm_room.reflection={255,226,194,255};
        warm_room.reflection_intensity=0.30f;
        warm_room.shininess=14.0f;
        if (local >= 500 && local < 1040) {
            const Vec3 spotlight_target=model("room.spotlight").position;
            warm_room.spot.enabled=true;
            warm_room.spot.position={spotlight_target.x,spotlight_target.y+1800.0f,spotlight_target.z};
            warm_room.spot.direction={0,-1,0};
            warm_room.spot.color={255,231,184,255};
            warm_room.spot.intensity=2.15f*std::clamp((local-500)/18.0f,0.0f,1.0f);
            warm_room.spot.range=3600.0f;
            warm_room.spot.inner_cone=0.94f;
            warm_room.spot.outer_cone=0.80f;
        }
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
            if (local >= 500) {
                // The source positions the visual emitter for Mario after
                // constructing it; the display-list descriptor itself is at
                // the origin. The manifest keeps the artwork and actual
                // light rig in the same place without scene-code asset data.
                renderer_->draw(r,model("room.spotlight"),camera,static_cast<float>(local-500),
                                {255,244,210,105},warm_room);
            }
            if (local >= 860) renderer_->draw(r,model("room.snap"),camera,static_cast<float>(local-860),
                                              {255,255,255,255},warm_room);
            if (local < 280) renderer_->draw(r,model("room.logo"),camera,static_cast<float>(local),
                                             {255,255,255,255},warm_room);
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
            if (local >= 1140) {
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
    int total_duration_{};
    bool done_{};
    SceneResourceManager* resources_{};
    Scene3DLoader* loader_{};
    std::unique_ptr<Scene3DRenderer> renderer_;
};

} // namespace

std::unique_ptr<Scene> make_opening_scene() { return std::make_unique<OpeningScene>(); }

} // namespace sagas
