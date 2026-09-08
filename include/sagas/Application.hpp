#pragma once

#include <sagas/Scene.hpp>
#include <sagas/SceneResources.hpp>

#include <chrono>

struct SDL_Window;
struct SDL_Gamepad;

namespace sagas {

struct ApplicationOptions {
    std::filesystem::path asset_root{SAGAS_DEFAULT_ASSET_ROOT};
    bool start_at_title{};
    bool start_at_menu{};
    bool start_at_select{},start_at_battle{};
    bool headless{};
    int frame_limit{};
    bool capture_only{};
    std::filesystem::path capture_path;
    std::filesystem::path controls_path{"controls.cfg"};
};

class Application final {
public:
    explicit Application(ApplicationOptions options);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    int run();
private:
    InputState poll_input();
    void load_controls();
    void save_controls();
    void draw_controls();
    struct Binding { int key{},button{-1}; };
    std::array<Binding,8> bindings_{};
    bool controls_open_{},binding_wait_{},tap_jump_{true};
    int control_row_{};
    std::string controls_error_;
    ApplicationOptions options_;
    SDL_Window* window_{};
    SDL_Gamepad* gamepad_{};
    std::unique_ptr<AssetRepository> assets_;
    std::unique_ptr<RenderEngine> render_;
    std::unique_ptr<AudioEngine> audio_;
    std::unique_ptr<SceneResourceManager> resources_;
    PhysicsWorld physics_;
    std::unique_ptr<Services> services_;
    std::unique_ptr<SceneMachine> scenes_;
};

} // namespace sagas
