#include <sagas/Engine.hpp>
#include <sagas/SceneDescriptors.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace sagas {
namespace {

class TitleScene final : public Scene {
public:
    void enter(Services& services) override { services.audio.stop(); }
    void update(Services& services, const InputState& input, float) override {
        ++tic_;
        if ((input.accept_pressed || input.start_pressed) && tic_ >= static_cast<int>(layout_value("title.start_tic",170))) {
            services.audio.play(AudioCue::TitlePressStart);
            accepted_ = 3;
        }
        if (accepted_ > 0 && --accepted_ == 0) proceed_ = true;
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0, 0, 0, 255});
        const auto fire = ui_numbered("title.fire_prefix", (tic_ % 30) + 1);
        const std::array<Color, 7> colors{{{255,255,255,255}, {255,240,155,255}, {255,255,100,255},
                                          {255,209,209,255}, {230,255,230,255}, {255,226,184,255},
                                          {255,210,148,255}}};
        const auto color_index=static_cast<std::size_t>((tic_/260)%colors.size());
        const auto next_color=colors[(color_index+1)%colors.size()];
        const float color_mix=std::clamp((tic_%260-180)/80.0f,0.0f,1.0f);
        const auto blend_channel=[&](std::uint8_t from,std::uint8_t to) {
            return static_cast<std::uint8_t>(std::lround(from+(to-from)*color_mix));
        };
        const Color tint{blend_channel(colors[color_index].r,next_color.r),
                         blend_channel(colors[color_index].g,next_color.g),
                         blend_channel(colors[color_index].b,next_color.b),255};
        r.sprite_rect(fire, 0, 0, 320, 240, tint);
        const auto next_fire = ui_numbered("title.fire_prefix", ((tic_ + 17) % 30) + 1);
        r.sprite_rect(next_fire, 0, 0, 320, 240, {tint.r, tint.g, tint.b, 100});

        r.sprite(ui_path("title.copyright"), {layout_value("title.copyright_x",160), layout_value("title.copyright_y",208)}, {1,1}, {245,231,180,255});
        r.sprite(ui_path("title.logo_anim"), {layout_value("title.logo_anim_x",260), layout_value("title.logo_anim_y",60)}, {1, 1}, {255, 48, 0, 36});
        if (tic_ >= static_cast<int>(layout_value("title.start_tic",170))) {
            constexpr float scale = 1.0f;
            const Color yellow{255, 254, 42, 255};
            r.sprite(ui_path("title.cutout"), {layout_value("title.cutout_x",157), layout_value("title.cutout_y",94)}, {scale, scale},
                     {0,0,0,yellow.a});
            r.sprite(ui_path("title.smash"), {layout_value("title.smash_x",161), layout_value("title.smash_y",88)}, {scale, scale}, {255,255,255,yellow.a});
            r.sprite(ui_path("title.super"), {layout_value("title.super_x",55), layout_value("title.super_y",96)}, {scale, scale}, yellow);
            r.sprite(ui_path("title.bros"), {layout_value("title.bros_x",268), layout_value("title.bros_y",96)}, {scale, scale}, yellow);
            r.sprite(ui_path("title.tm"), {layout_value("title.tm_x",270), layout_value("title.tm_y",132)}, {scale, scale}, {0,0,0,yellow.a});
        }
        if (tic_ >= static_cast<int>(layout_value("title.press_tic",280))) {
            const float wave = 0.72f + 0.28f * std::sin(tic_ * std::numbers::pi_v<float> / 20.0f);
            const auto alpha = static_cast<std::uint8_t>(255 * wave);
            r.sprite(ui_path("title.press_start"), {layout_value("title.press_x",162), layout_value("title.press_y",177)}, {1,1},
                     accepted_ ? Color{255,255,255,255} : Color{255,255,255,alpha});
        }
        r.end();
    }
    std::unique_ptr<Scene> next() override { return proceed_ ? make_menu_scene() : nullptr; }
private:
    int tic_{169};
    int accepted_{};
    bool proceed_{};
};

} // namespace

std::unique_ptr<Scene> make_title_scene() { return std::make_unique<TitleScene>(); }

} // namespace sagas
