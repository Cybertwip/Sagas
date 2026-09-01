#pragma once

#include <sagas/Scene.hpp>

#include <chrono>

struct SDL_Window;

namespace sagas {

struct ApplicationOptions {
    std::filesystem::path asset_root{SAGAS_DEFAULT_ASSET_ROOT};
    bool start_at_title{};
    bool headless{};
    int frame_limit{};
    std::filesystem::path capture_path;
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
    ApplicationOptions options_;
    SDL_Window* window_{};
    std::unique_ptr<AssetRepository> assets_;
    std::unique_ptr<RenderEngine> render_;
    std::unique_ptr<AudioEngine> audio_;
    PhysicsWorld physics_;
    std::unique_ptr<Services> services_;
    std::unique_ptr<SceneMachine> scenes_;
};

} // namespace sagas
