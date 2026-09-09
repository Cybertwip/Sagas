#include <sagas/Engine.hpp>

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
        if ((input.accept_pressed || input.start_pressed) && tic_ >= 170) {
            services.audio.play(AudioCue::TitlePressStart);
            accepted_ = 3;
        }
        if (accepted_ > 0 && --accepted_ == 0) proceed_ = true;
    }
    void draw(Services& services) override {
        auto& r = services.render;
        r.begin({0, 0, 0, 255});
        const auto fire = "textures/MNTitleFireAnim/Frame" + std::to_string((tic_ % 30) + 1) + ".png";
        const std::array<Color, 7> colors{{{255,255,255,255}, {255,240,155,255}, {255,255,100,255},
                                          {255,209,209,255}, {230,255,230,255}, {255,226,184,255},
                                          {255,210,148,255}}};
        // Remix changes the fire family periodically and eases into the next
        // color over its final 80 ticks.  Cycle deterministically here so the
        // title stays alive without a discontinuous random flash.
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
        const auto next_fire = "textures/MNTitleFireAnim/Frame" + std::to_string(((tic_ + 17) % 30) + 1) + ".png";
        r.sprite_rect(next_fire, 0, 0, 320, 240, {tint.r, tint.g, tint.b, 100});

        // Keep the fire continuous to the canvas edges. The extracted border
        // and footer panels otherwise introduce conspicuous inset rectangles.
        r.sprite("textures/MNTitle/Copyright.png", {160, 208}, {1,1}, {245,231,180,255});
        r.sprite("textures/MNTitle/LogoAnimFull.png", {260, 60}, {1, 1}, {255, 48, 0, 36});
        if (tic_ >= 170) {
            // Tick 170 calls mnTitleSetEndLogoPosition and exposes the label
            // link.  Keep that snapped final state; the previous uniform
            // zero-to-one scale was not the source joint animation and made
            // the assembled logo appear to replace/disappear abruptly.
            constexpr float scale = 1.0f;
            const Color yellow{255, 254, 42, 255};
            r.sprite("textures/MNTitle/Cutout.png", {157, 94}, {scale, scale},
                     {0,0,0,yellow.a});
            r.sprite("textures/MNTitle/Smash.png", {161, 88}, {scale, scale}, {255,255,255,yellow.a});
            r.sprite("textures/MNTitle/Super.png", {55, 96}, {scale, scale}, yellow);
            r.sprite("textures/MNTitle/Bros.png", {268, 96}, {scale, scale}, yellow);
            r.sprite("textures/MNTitle/TMUnk.png", {270, 132}, {scale, scale}, {0,0,0,yellow.a});
        }
        if (tic_ >= 280) {
            const float wave = 0.72f + 0.28f * std::sin(tic_ * std::numbers::pi_v<float> / 20.0f);
            const auto alpha = static_cast<std::uint8_t>(255 * wave);
            r.sprite("textures/MNTitle/PressStart.png", {162, 177}, {1,1},
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
