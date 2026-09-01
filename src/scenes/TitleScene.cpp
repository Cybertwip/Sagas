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
        if (input.accept_pressed && tic_ >= 170) {
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
        const auto tint = colors[3];
        r.sprite_at(fire, {-32,-16}, {12.0f, 8.5f}, tint);
        const auto next_fire = "textures/MNTitleFireAnim/Frame" + std::to_string(((tic_ + 17) % 30) + 1) + ".png";
        r.sprite_at(next_fire, {8,8}, {9.5f, 7.0f}, {tint.r, tint.g, tint.b, 210});

        // Header and footer belong to the fixed US layout and do not take
        // part in the logo-label transition.
        r.sprite("textures/MNTitle/BorderUpper.png", {160, 15}, {1,1}, {20,18,6,255});
        r.sprite("textures/MNTitle/Copyright.png", {160, 208}, {1,1}, {183,174,124,255});

        // The red full-logo layer is the persistent low-alpha backing logo
        // in the original final layout.  It does not get removed when the
        // individual yellow labels finish their transition.
        r.sprite("textures/MNTitle/LogoAnimFull.png", {260, 60}, {1, 1}, {255, 0, 0, 76});
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
