#pragma once

#include <sagas/Audio.hpp>
#include <sagas/Render.hpp>

namespace sagas {

class SceneResourceManager;
struct Services {
    AssetRepository& assets;
    RenderEngine& render;
    AudioEngine& audio;
    PhysicsWorld& physics;
    SceneResourceManager& resources;
};

class Scene {
public:
    virtual ~Scene() = default;
    virtual void enter(Services&) {}
    virtual void update(Services&, const InputState&, float fixed_seconds) = 0;
    virtual void draw(Services&) = 0;
    [[nodiscard]] virtual std::unique_ptr<Scene> next() { return {}; }
};

class SceneMachine final {
public:
    SceneMachine(std::unique_ptr<Scene> initial, Services& services);
    void update(const InputState& input, float fixed_seconds);
    void draw();
private:
    Services& services_;
    std::unique_ptr<Scene> scene_;
};

[[nodiscard]] std::unique_ptr<Scene> make_startup_scene();
[[nodiscard]] std::unique_ptr<Scene> make_opening_scene();
[[nodiscard]] std::unique_ptr<Scene> make_title_scene();

} // namespace sagas
