#pragma once

#include <sagas/Audio.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/Render.hpp>

namespace sagas {

class SceneResourceManager;
struct Services {
    AssetRepository& assets;
    RenderEngine& render;
    AudioEngine& audio;
    PhysicsWorld& physics;
    SceneResourceManager& resources;
    bool deterministic_clock{};
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
[[nodiscard]] std::unique_ptr<Scene> make_menu_scene();
[[nodiscard]] std::unique_ptr<Scene> make_character_select_scene(int stock = 3, bool team = false,
                                                                bool one_player = false);
// Read-only simulation inspection for replay verification and native regression captures.
[[nodiscard]] std::span<const FighterBody> battle_fighters(const Scene& scene);
[[nodiscard]] std::size_t battle_projectile_count(const Scene& scene, unsigned weapon);
[[nodiscard]] std::unique_ptr<Scene> make_battle_scene(std::vector<FighterKind> fighters, int stock = 3, std::vector<int> ports = {}, std::vector<std::string> models = {});
[[nodiscard]] std::unique_ptr<Scene> make_battle_scene(FighterKind p1, FighterKind p2, int stock = 3);

} // namespace sagas
