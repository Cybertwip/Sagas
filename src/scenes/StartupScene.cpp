#include <sagas/Engine.hpp>
#include <sagas/SceneDescriptors.hpp>
#include <sagas/SceneResources.hpp>

#include <algorithm>

namespace sagas {
namespace {

class StartupScene final : public Scene {
public:
    void enter(Services& services) override {
        services.resources.load_manifest(ui_path("opening.manifest"));
        services.resources.prefetch("room.base");
        services.audio.preload_music(ui_path("opening.music"), 1.0f);
    }
    void update(Services& services, const InputState& input, float) override {
        ++frame_;
        if (frame_ >= 8 && (input.accept_pressed || input.cancel_pressed || input.skip_pressed)) {
            skip_ = done_ = true;
        } else if (frame_ >= 53 && services.resources.ready("room.base") &&
                   services.audio.music_ready(ui_path("opening.music"))) {
            done_ = true;
        }
    }
    void draw(Services& services) override {
        services.render.begin({0, 0, 0, 255});
        const float step = static_cast<float>(16 - std::min(frame_, 16));
        const float rest = layout_value("startup.logo_rest_y", 65.0f);
        const float y = frame_ < 16 ? rest + (38.75f / 64.0f) * step * step : rest;
        services.render.sprite(ui_path("startup.logo"), {layout_value("startup.logo_x", 160), y + 54});
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

} // namespace

std::unique_ptr<Scene> make_startup_scene() { return std::make_unique<StartupScene>(); }

} // namespace sagas
