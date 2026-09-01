#include <sagas/Engine.hpp>
#include <sagas/N64.hpp>
#include <sagas/Scene3D.hpp>

#include <SDL3/SDL.h>
#include <png.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace sagas {
namespace {

#if 0 // Audio implementation moved to Audio.cpp; retained until scene split lands.
[[noreturn]] void fail(std::string message) {
    if (const char* detail = SDL_GetError(); detail && *detail) message += ": " + std::string(detail);
    throw std::runtime_error(std::move(message));
}

std::uint16_t be16(const std::byte* p) {
    return (std::to_integer<std::uint16_t>(p[0]) << 8) | std::to_integer<std::uint16_t>(p[1]);
}
std::uint32_t be32(const std::byte* p) {
    return (std::to_integer<std::uint32_t>(p[0]) << 24) |
           (std::to_integer<std::uint32_t>(p[1]) << 16) |
           (std::to_integer<std::uint32_t>(p[2]) << 8) | std::to_integer<std::uint32_t>(p[3]);
}
bool tag(const std::byte* p, const char* text) { return std::memcmp(p, text, 4) == 0; }

double extended80(const std::byte* p) {
    const auto exponent = be16(p);
    std::uint64_t mantissa{};
    for (int i = 0; i < 8; ++i) mantissa = (mantissa << 8) | std::to_integer<unsigned>(p[i + 2]);
    if ((exponent & 0x7fffU) == 0 && mantissa == 0) return 0;
    const double value = std::ldexp(static_cast<double>(mantissa),
                                    static_cast<int>(exponent & 0x7fffU) - 16383 - 63);
    return exponent & 0x8000U ? -value : value;
}

struct Pcm { int rate{}; std::vector<std::int16_t> samples; };
Pcm load_aiff(std::span<const std::byte> bytes, float gain) {
    if (bytes.size() < 12 || !tag(bytes.data(), "FORM") ||
        (!tag(bytes.data() + 8, "AIFF") && !tag(bytes.data() + 8, "AIFC")))
        throw std::runtime_error("audio is not AIFF PCM");
    int channels{}, bits{}, rate{};
    std::uint32_t frames{};
    const std::byte* sound{};
    std::size_t sound_size{};
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const auto size = be32(bytes.data() + at + 4);
        const auto body = at + 8;
        if (body + size > bytes.size()) break;
        if (tag(bytes.data() + at, "COMM") && size >= 18) {
            channels = be16(bytes.data() + body);
            frames = be32(bytes.data() + body + 2);
            bits = be16(bytes.data() + body + 6);
            rate = static_cast<int>(std::lround(extended80(bytes.data() + body + 8)));
            if (size >= 22 && !tag(bytes.data() + body + 18, "NONE"))
                throw std::runtime_error("compressed AIFC is not a runtime PCM asset");
        } else if (tag(bytes.data() + at, "SSND") && size >= 8) {
            const auto offset = be32(bytes.data() + body);
            if (8ULL + offset <= size) {
                sound = bytes.data() + body + 8 + offset;
                sound_size = size - 8 - offset;
            }
        }
        at = body + size + (size & 1U);
    }
    if (!sound || channels < 1 || bits != 16 || rate <= 0) throw std::runtime_error("unsupported AIFF layout");
    const auto available = sound_size / 2 / static_cast<std::size_t>(channels);
    const auto count = std::min<std::size_t>(frames, available);
    Pcm pcm{rate, {}};
    pcm.samples.reserve(count);
    for (std::size_t frame = 0; frame < count; ++frame) {
        int mixed{};
        for (int channel = 0; channel < channels; ++channel) {
            const auto index = (frame * channels + channel) * 2;
            mixed += static_cast<std::int16_t>(be16(sound + index));
        }
        pcm.samples.push_back(static_cast<std::int16_t>(std::clamp(mixed * gain / channels, -32768.0f, 32767.0f)));
    }
    return pcm;
}

std::uint32_t le32(const std::byte* p) {
    return std::to_integer<std::uint32_t>(p[0]) | (std::to_integer<std::uint32_t>(p[1]) << 8) |
           (std::to_integer<std::uint32_t>(p[2]) << 16) | (std::to_integer<std::uint32_t>(p[3]) << 24);
}

struct MusicSound {
    int program{}, velocity_min{}, velocity_max{}, key_min{}, key_max{}, key_base{}, detune{}, wave{};
    int instrument_volume{}, sample_volume{}, attack_us{}, decay_us{}, release_us{}, attack_volume{}, decay_volume{};
    int loop_start{}, loop_end{};
};
struct MusicEvent { std::uint32_t tick{}, frame{}; std::uint8_t kind{}, channel{}, a{}, b{}; };
struct MusicPackage { unsigned division{}, tempo{}; std::vector<MusicSound> sounds; std::vector<MusicEvent> events; };

MusicPackage load_music_package(std::span<const std::byte> bytes) {
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "SGM1", 4) != 0) throw std::runtime_error("invalid Sagas music package");
    MusicPackage music{le32(bytes.data()+4), le32(bytes.data()+8), {}, {}};
    const auto sound_count = le32(bytes.data()+12), track_count = le32(bytes.data()+16);
    std::size_t at = 20;
    for (std::uint32_t i = 0; i < sound_count; ++i) {
        if (at + 68 > bytes.size()) throw std::runtime_error("truncated Sagas sound bank");
        std::array<int, 17> value{};
        for (int& field : value) { field = static_cast<int>(le32(bytes.data()+at)); at += 4; }
        music.sounds.push_back({value[0],value[1],value[2],value[3],value[4],value[5],value[6],value[7],
                                value[8],value[9],value[10],value[11],value[12],value[13],value[14],value[15],value[16]});
    }
    for (std::uint32_t track = 0; track < track_count; ++track) {
        if (at + 16 > bytes.size()) throw std::runtime_error("truncated Sagas track table");
        const auto track_id = le32(bytes.data()+at); (void)track_id;
        const auto loop_start = le32(bytes.data()+at+4), loop_end = le32(bytes.data()+at+8); (void)loop_start; (void)loop_end;
        const auto count = le32(bytes.data()+at+12); at += 16;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (at + 8 > bytes.size()) throw std::runtime_error("truncated Sagas music event");
            music.events.push_back({le32(bytes.data()+at), 0,
                                    std::to_integer<std::uint8_t>(bytes[at+4]), std::to_integer<std::uint8_t>(bytes[at+5]),
                                    std::to_integer<std::uint8_t>(bytes[at+6]), std::to_integer<std::uint8_t>(bytes[at+7])});
            at += 8;
        }
    }
    std::stable_sort(music.events.begin(), music.events.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    double frame{};
    std::uint32_t previous_tick{};
    unsigned tempo = music.tempo;
    for (std::size_t i = 0; i < music.events.size();) {
        const auto tick = music.events[i].tick;
        frame += static_cast<double>(tick - previous_tick) * tempo * 32000.0 /
                 (static_cast<double>(music.division) * 1'000'000.0);
        std::size_t end = i;
        while (end < music.events.size() && music.events[end].tick == tick) {
            music.events[end].frame = static_cast<std::uint32_t>(std::llround(frame));
            if (music.events[end].kind == 5)
                tempo = (music.events[end].channel << 16) | (music.events[end].a << 8) | music.events[end].b;
            ++end;
        }
        previous_tick = tick;
        i = end;
    }
    return music;
}
#endif

class StartupScene final : public Scene {
public:
    void update(Services&, const InputState& input, float) override {
        ++frame_;
        if (frame_ >= 8 && (input.accept_pressed || input.cancel_pressed || input.skip_pressed)) {
            skip_ = done_ = true;
        } else if (frame_ >= 53) done_ = true;
    }
    void draw(Services& services) override {
        services.render.begin({0, 0, 0, 255});
        const float step = static_cast<float>(16 - std::min(frame_, 16));
        const float y = frame_ < 16 ? 65.0f + (38.75f / 64.0f) * step * step : 65.0f;
        services.render.sprite("textures/N64Logo.png", {160, y + 54});
        float fade{};
        if (frame_ < 16) fade = 1.0f - frame_ / 16.0f;
        else if (frame_ >= 40) fade = std::min(1.0f, (frame_ - 40) / 10.0f);
        if (fade > 0) services.render.fill(0, 0, 320, 240, {0, 0, 0, static_cast<std::uint8_t>(fade * 255)});
        services.render.end();
    }
    std::unique_ptr<Scene> next() override {
        if (!done_) return {};
        return skip_ ? make_title_scene() : make_opening_scene();
    }
private:
    int frame_{};
    bool done_{};
    bool skip_{};
};

class OpeningScene final : public Scene {
public:
    void enter(Services& services) override {
        services.audio.play_music("audio/opening.sgm", 0.72f);
        archive_ = std::make_unique<n64::RelocArchive>(services.assets);
        loader_ = std::make_unique<Scene3DLoader>(*archive_);
        renderer_ = std::make_unique<Scene3DRenderer>(*archive_);
        room_background_ = loader_->model("llMVCommonRoomBackgroundDObjDesc", {}, GeometryLayout::DisplayListLinks);
        room_sunlight_ = loader_->display_list("llMVCommonRoomSunlightDisplayList", GeometryLayout::DisplayListLinks);
        room_desk_ = loader_->model("llMVCommonRoomDeskDObjDesc", {}, GeometryLayout::Direct);
        room_outside_ = loader_->display_list("llMVCommonRoomOutsideDisplayList", GeometryLayout::DisplayListLinks);
        room_haze_ = loader_->display_list("llMVCommonRoomHazeDisplayList", GeometryLayout::DisplayListLinks);
        room_books_ = loader_->model("llMVCommonRoomBooksDObjDesc", "llMVCommonRoomBooksAnimJoint", GeometryLayout::Direct);
        room_pencils_ = loader_->model("llMVCommonRoomPencilsDObjDesc", "llMVCommonRoomPencilsAnimJoint", GeometryLayout::Direct);
        room_lamp_ = loader_->model("llMVCommonRoomLampDObjDesc", "llMVCommonRoomLampAnimJoint", GeometryLayout::Direct);
        room_tissues_ = loader_->display_list("llMVCommonRoomTissuesDisplayList");
        if (const auto animation = archive_->symbol("llMVCommonRoomTissuesAnimJoint"))
            room_tissues_.animation[0] = animation;
        room_desk_ground_ = loader_->model("llMVCommonRoomDeskGroundDObjDesc", {}, GeometryLayout::DisplayListLinks);
        room_logo_ = loader_->model("llMVCommonRoomLogoDObjDesc", {}, GeometryLayout::DisplayListLinks);
        n64::AnimationDecoder animation(*archive_);
        const auto animated_fighter = [&](std::string_view descriptor, std::uint32_t file) {
            auto model = loader_->model(descriptor, {}, GeometryLayout::Direct);
            model.animation = animation.table({file,0},model.nodes.size());
            return model;
        };
        boss_pose1_ = animated_fighter("llBossModelJointTreeDObjDesc",458);
        boss_pose2_ = animated_fighter("llBossModelJointTreeDObjDesc",459);
        boss_pose3_ = animated_fighter("llBossModelJointTreeDObjDesc",460);
        mario_pickup_ = animated_fighter("llMarioModelJointTreeDObjDesc",362);
        mario_fall_ = animated_fighter("llMarioModelJointTreeDObjDesc",363);
        mario_revival_ = animated_fighter("llMarioModelJointTreeDObjDesc",364);
        link_fall_ = animated_fighter("llLinkModelJointTreeDObjDesc",409);
        link_fall_.position = {872.32495f,4038.8640f,-4734.6001f};
        yoster_nest_ = loader_->model("llMVOpeningYosterNestDObjDesc");
        yoster_ground_ = loader_->model("llMVOpeningYosterGroundDObjDesc", "llMVOpeningYosterGroundAnimJoint");
        cliff_hills_ = loader_->model("llMVOpeningCliffHillsDObjDesc", {}, GeometryLayout::Direct);
        cliff_ocarina_ = loader_->model("llMVOpeningCliffOcarinaDObjDesc", "llMVOpeningCliffOcarinaAnimJoint", GeometryLayout::Direct);
        yamabuki_legs_ = loader_->model("llMVOpeningYamabukiLegsDObjDesc", "llMVOpeningYamabukiLegsAnimJoint", GeometryLayout::Direct);
        yamabuki_shadow_ = loader_->model("llMVOpeningYamabukiLegsShadowDObjDesc", "llMVOpeningYamabukiLegsShadowAnimJoint");
        yamabuki_ball_ = loader_->model("llMVOpeningYamabukiMBallDObjDesc", "llMVOpeningYamabukiMBallAnimJoint");
        sector_great_fox_ = loader_->model("llMVOpeningSectorGreatFoxDObjDesc", "llMVOpeningSectorGreatFoxAnimJoint");
        standoff_ground_ = loader_->display_list("llMVOpeningStandoffGroundDisplayList");
        standoff_lightning_ = loader_->model("llMVOpeningStandoffLightningDObjDesc", "llMVOpeningStandoffLightningAnimJoint");
    }
    void update(Services&, const InputState& input, float) override {
        ++tic_;
        if (tic_ >= 10 && (input.accept_pressed || input.cancel_pressed || input.skip_pressed)) done_ = true;
        if (tic_ >= total_duration) done_ = true;
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0, 0, 0, 255});
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
                renderer_->draw(r, cliff_hills_, camera, local);
                renderer_->draw(r, cliff_ocarina_, camera, local);
                break;
            }
            case Segment::Yamabuki: {
                wallpaper(r, "MVOpeningYamabuki/Wallpaper.png");
                const auto camera = loader_->camera("llMVOpeningYamabukiCamAnimJoint", local);
                renderer_->draw(r, yamabuki_shadow_, camera, local, {45,45,55,120});
                renderer_->draw(r, yamabuki_legs_, camera, local);
                renderer_->draw(r, yamabuki_ball_, camera, local);
                break;
            }
            case Segment::Jungle: fighter(r, local, "MVOpeningPortraitsSet2/Donkey.png", {22, 67, 32, 255}); break;
            case Segment::Yoster: {
                wallpaper(r, "StageYoshi.png");
                const auto camera = loader_->camera("llMVOpeningYosterCamAnimJoint", local);
                renderer_->draw(r, yoster_nest_, camera, local);
                renderer_->draw(r, yoster_ground_, camera, local);
                break;
            }
            case Segment::Sector: {
                wallpaper(r, "MVOpeningSectorWallpaper.png");
                renderer_->draw(r, sector_great_fox_, loader_->camera("llMVOpeningSectorCamAnimJoint", local), local);
                r.sprite("textures/MVOpeningSector/Cockpit.png", {160,120});
                break;
            }
            case Segment::Standoff: {
                wallpaper(r, "MVOpeningStandoffWallpaper.png", {2,2});
                const auto camera = loader_->camera("llMVOpeningStandoffCamAnimJoint", local);
                renderer_->draw(r, standoff_ground_, camera, local);
                renderer_->draw(r, standoff_lightning_, camera, local);
                break;
            }
            case Segment::Clash: clash(r, local); break;
            case Segment::Newcomers: newcomers(r, local); break;
        }
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
        if (local < 1040) {
            renderer_->draw(r, room_outside_, camera, camera_frame, {210,226,255,255}, warm_room);
            renderer_->draw(r, room_haze_, camera, camera_frame, {220,225,235,150}, warm_room);
            renderer_->draw(r, room_background_, camera, static_cast<float>(local), {255,255,255,255}, warm_room);
            if (local < 450) renderer_->draw(r, room_sunlight_, camera, camera_frame, {255,240,190,150}, warm_room);
            renderer_->draw(r, room_desk_, camera, camera_frame, {255,255,255,255}, warm_room);
            const float prop_frame = static_cast<float>(std::max(local - 560, 0));
            renderer_->draw(r, room_books_, camera, prop_frame, {255,255,255,255}, warm_room);
            if (local >= 280) renderer_->draw(r, room_pencils_, camera, prop_frame, {255,255,255,255}, warm_room);
            renderer_->draw(r, room_lamp_, camera, prop_frame, {255,255,255,255}, warm_room);
            renderer_->draw(r, room_tissues_, camera, prop_frame, {255,255,255,255}, warm_room);
            if (local < 280) renderer_->draw(r,room_logo_,camera,static_cast<float>(local),
                                             {255,255,255,255},warm_room);
            const Model3D& boss = local < 560 ? boss_pose1_ : (local < 860 ? boss_pose2_ : boss_pose3_);
            const float boss_frame = static_cast<float>(local < 560 ? local : (local < 860 ? local-560 : local-860));
            renderer_->draw(r,boss,camera,boss_frame,{255,255,255,255},warm_room);
            if (local >= 280) {
                if (local < 380) renderer_->draw(r,mario_pickup_,camera,static_cast<float>(local-280),{255,255,255,255},warm_room);
                else renderer_->draw(r,mario_fall_,camera,static_cast<float>(local-380),{255,255,255,255},warm_room);
            }
            if (local >= 695) renderer_->draw(r,link_fall_,camera,static_cast<float>(local-695),{255,255,255,255},warm_room);
        } else {
            wallpaper(r, "MVOpeningRoomWallpaper.png");
            renderer_->draw(r, room_desk_ground_, camera, static_cast<float>(std::max(local - 1060, 0)),
                            {255,255,255,255}, warm_room);
            if (local >= 1140) renderer_->draw(r,mario_revival_,camera,static_cast<float>(local-1140),
                                               {255,255,255,255},warm_room);
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
    int tic_{};
    bool done_{};
    std::unique_ptr<n64::RelocArchive> archive_;
    std::unique_ptr<Scene3DLoader> loader_;
    std::unique_ptr<Scene3DRenderer> renderer_;
    Model3D yoster_nest_, yoster_ground_, cliff_hills_, cliff_ocarina_;
    Model3D yamabuki_legs_, yamabuki_shadow_, yamabuki_ball_;
    Model3D sector_great_fox_, standoff_ground_, standoff_lightning_;
    Model3D room_background_, room_sunlight_, room_desk_, room_outside_, room_haze_;
    Model3D room_books_, room_pencils_, room_lamp_, room_tissues_, room_desk_ground_, room_logo_;
    Model3D boss_pose1_, boss_pose2_, boss_pose3_;
    Model3D mario_pickup_, mario_fall_, mario_revival_, link_fall_;
};

class TitleScene final : public Scene {
public:
    void enter(Services& services) override { services.audio.stop(); }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        if (input.accept_pressed && tic_ >= 170) {
            services.audio.play("audio/B1_sounds1/wave_021.aiff", 0.86f);
            accepted_ = 12;
        }
        if (accepted_ > 0) --accepted_;
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0, 0, 0, 255});
        const auto fire = "textures/MNTitleFireAnim/Frame" + std::to_string((tic_ % 30) + 1) + ".png";
        const std::array<Color, 7> colors{{{255,255,255,255}, {255,240,155,255}, {255,255,100,255},
                                          {255,209,209,255}, {230,255,230,255}, {255,226,184,255},
                                          {255,210,148,255}}};
        const auto tint = colors[3];
        r.sprite(fire, {160, 120}, {12.0f, 8.5f}, tint);
        r.sprite(fire, {160, 120}, {9.5f, 7.0f}, {tint.r, tint.g, tint.b, 210});

        if (tic_ < 220) {
            const float pulse = tic_ < 170 ? 0.45f : std::clamp((tic_ - 170) / 50.0f, 0.0f, 1.0f);
            r.sprite("textures/MNTitle/LogoAnimFull.png", {260, 60}, {pulse, pulse}, {255, 0, 0, 180});
        }
        if (tic_ >= 170) {
            const float scale = std::clamp((tic_ - 170) / 50.0f, 0.0f, 1.0f);
            const Color yellow{255, 254, 42, static_cast<std::uint8_t>(255 * scale)};
            r.sprite("textures/MNTitle/Cutout.png", {157, 94}, {scale, scale}, yellow);
            r.sprite("textures/MNTitle/Smash.png", {161, 88}, {scale, scale}, {255,255,255,yellow.a});
            r.sprite("textures/MNTitle/Super.png", {55, 96}, {scale, scale}, yellow);
            r.sprite("textures/MNTitle/Bros.png", {268, 96}, {scale, scale}, yellow);
            r.sprite("textures/MNTitle/TMUnk.png", {270, 132}, {scale, scale}, {0,0,0,yellow.a});
            r.sprite("textures/MNTitle/TM.png", {277, 157}, {scale, scale}, {21,19,6,yellow.a});
            r.sprite("textures/MNTitle/BorderUpper.png", {160, 15}, {1,1}, {20,18,6,yellow.a});
        }
        if (tic_ >= 240) r.sprite("textures/MNTitle/Copyright.png", {160, 208}, {1,1}, {183,174,124,255});
        if (tic_ >= 280) {
            const float wave = 0.72f + 0.28f * std::sin(tic_ * std::numbers::pi_v<float> / 20.0f);
            const auto alpha = static_cast<std::uint8_t>(255 * wave);
            r.sprite("textures/MNTitle/PressStart.png", {162, 177}, {1,1},
                     accepted_ ? Color{255,255,255,255} : Color{255,255,255,alpha});
        }
        r.end();
    }
private:
    int tic_{169};
    int accepted_{};
};

} // namespace

#if 0 // Audio implementation moved to Audio.cpp.
AudioEngine::AudioEngine(AssetRepository& assets) : assets_(assets) {}
AudioEngine::~AudioEngine() { if (stream_) SDL_DestroyAudioStream(stream_); }
void AudioEngine::stop() { if (stream_) SDL_ClearAudioStream(stream_); }
void AudioEngine::queue(std::span<const std::int16_t> samples, int rate, int channels) {
    if (stream_) SDL_DestroyAudioStream(stream_);
    const SDL_AudioSpec spec{SDL_AUDIO_S16, channels, rate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) fail("audio device open failed");
    if (!SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size_bytes()))) fail("audio queue failed");
    if (!SDL_ResumeAudioStreamDevice(stream_)) fail("audio resume failed");
}
void AudioEngine::play(std::string_view logical, float gain) {
    const auto bytes = assets_.blob(logical);
    auto pcm = load_aiff(*bytes, gain);
    queue(pcm.samples, pcm.rate);
}
void AudioEngine::play_music(std::string_view logical, float gain) {
    const auto package = load_music_package(*assets_.blob(logical));
    struct Voice { const MusicSound* sound{}; const Pcm* pcm{}; int channel{}, note{}; double position{}, step{}; std::uint64_t age{}, release_age{}; bool released{}; float gain{}; };
    std::array<int,16> programs{}, volumes{}, bends{};
    volumes.fill(127); bends.fill(8192);
    std::unordered_map<int, Pcm> waves;
    std::vector<Voice> voices;
    std::vector<std::int16_t> output;
    const auto last_event = package.events.empty() ? 0U : package.events.back().frame;
    const auto frame_count = std::max<std::uint32_t>(last_event + 160000U, 65U * 32000U);
    output.reserve(frame_count);
    std::size_t event_index{};
    auto get_wave = [&](int id) -> const Pcm* {
        if (!waves.contains(id)) {
            std::ostringstream name;
            name << "audio/B1_sounds1/wave_" << std::setw(3) << std::setfill('0') << id << ".aiff";
            waves.emplace(id, load_aiff(*assets_.blob(name.str()), 1.0f));
        }
        return &waves.at(id);
    };
    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
        while (event_index < package.events.size() && package.events[event_index].frame <= frame) {
            const auto& event = package.events[event_index++];
            const int channel = event.channel & 15;
            if (event.kind == 2) programs[channel] = event.a;
            else if (event.kind == 3 && event.a == 7) volumes[channel] = event.b;
            else if (event.kind == 4) bends[channel] = event.a | (event.b << 7);
            else if (event.kind == 1 || (event.kind == 0 && event.b == 0)) {
                for (auto& voice : voices) if (voice.channel == channel && voice.note == event.a && !voice.released) {
                    voice.released = true; voice.release_age = 0;
                }
            } else if (event.kind == 0) {
                const auto sound = std::find_if(package.sounds.begin(), package.sounds.end(), [&](const auto& item) {
                    return item.program == programs[channel] && event.a >= item.key_min && event.a <= item.key_max &&
                           event.b >= item.velocity_min && event.b <= item.velocity_max;
                });
                if (sound != package.sounds.end()) {
                    const auto* pcm = get_wave(sound->wave);
                    const float bend_cents = (bends[channel] - 8192) * (200.0f / 8192.0f);
                    const float cents = (event.a - sound->key_base) * 100.0f + sound->detune + bend_cents;
                    const float level = gain * event.b / 127.0f * volumes[channel] / 127.0f *
                                        sound->instrument_volume / 127.0f * sound->sample_volume / 127.0f;
                    voices.push_back({&*sound, pcm, channel, event.a, 0,
                                      std::pow(2.0, cents / 1200.0) * pcm->rate / 32000.0,
                                      0, 0, false, level});
                }
            }
        }
        double mixed{};
        for (auto& voice : voices) {
            if (!voice.pcm || voice.position >= voice.pcm->samples.size()) continue;
            const auto& sound = *voice.sound;
            const double age_us = voice.age * (1'000'000.0 / 32000.0);
            float envelope = sound.decay_volume / 127.0f;
            if (sound.attack_us > 0 && age_us < sound.attack_us)
                envelope = static_cast<float>(age_us / sound.attack_us) * sound.attack_volume / 127.0f;
            else if (sound.decay_us > 0 && age_us < sound.attack_us + sound.decay_us) {
                const float blend = static_cast<float>((age_us - sound.attack_us) / sound.decay_us);
                envelope = (sound.attack_volume + (sound.decay_volume-sound.attack_volume)*blend) / 127.0f;
            }
            if (voice.released) {
                const double release_us = voice.release_age++ * (1'000'000.0 / 32000.0);
                envelope *= sound.release_us > 0 ? std::max(0.0, 1.0-release_us/sound.release_us) : 0.0;
                if (envelope <= 0) { voice.pcm = nullptr; continue; }
            }
            const auto index = static_cast<std::size_t>(voice.position);
            const auto next = std::min(index + 1, voice.pcm->samples.size() - 1);
            const double fraction = voice.position - index;
            mixed += (voice.pcm->samples[index]*(1-fraction) + voice.pcm->samples[next]*fraction) * voice.gain * envelope;
            voice.position += voice.step; ++voice.age;
            if (!voice.released && sound.loop_end > sound.loop_start && voice.position >= sound.loop_end)
                voice.position = sound.loop_start + std::fmod(voice.position-sound.loop_start, sound.loop_end-sound.loop_start);
        }
        if ((frame & 4095U) == 0) voices.erase(std::remove_if(voices.begin(), voices.end(), [](const auto& v){ return !v.pcm; }), voices.end());
        output.push_back(static_cast<std::int16_t>(std::clamp(mixed, -32768.0, 32767.0)));
    }
    queue(output, 32000);
}
#endif

std::unique_ptr<Scene> make_startup_scene() { return std::make_unique<StartupScene>(); }
std::unique_ptr<Scene> make_opening_scene() { return std::make_unique<OpeningScene>(); }
std::unique_ptr<Scene> make_title_scene() { return std::make_unique<TitleScene>(); }

} // namespace sagas
