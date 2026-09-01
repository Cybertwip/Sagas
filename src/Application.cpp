#include <sagas/Application.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdexcept>

namespace sagas {
namespace {
[[noreturn]] void fail(std::string message) {
    if (const char* detail = SDL_GetError(); detail && *detail) message += ": " + std::string(detail);
    throw std::runtime_error(std::move(message));
}
}

Application::Application(ApplicationOptions options) : options_(std::move(options)) {
    if (options_.headless) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) fail("SDL initialization failed");
    const auto flags = options_.headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
    if (!SDL_CreateWindowAndRenderer("Sagas | Smash Remix", 960, 720, flags, &window_, &renderer_)) fail("window creation failed");
    assets_ = std::make_unique<AssetRepository>(options_.asset_root);
    render_ = std::make_unique<RenderEngine>(window_, renderer_, *assets_);
    audio_ = std::make_unique<AudioEngine>(*assets_);
    services_ = std::make_unique<Services>(Services{*assets_, *render_, *audio_, physics_});
    scenes_ = std::make_unique<SceneMachine>(options_.start_at_title ? make_title_scene() : make_startup_scene(), *services_);
}
Application::~Application() {
    scenes_.reset(); services_.reset(); audio_.reset(); render_.reset(); assets_.reset();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}
InputState Application::poll_input() {
    InputState input;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) input.quit = true;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            input.accept_pressed |= event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE || event.key.key == SDLK_A;
            input.cancel_pressed |= event.key.key == SDLK_ESCAPE || event.key.key == SDLK_B;
            input.skip_pressed |= event.key.key == SDLK_S;
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            input.accept_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH || event.gbutton.button == SDL_GAMEPAD_BUTTON_START;
            input.cancel_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
        }
    }
    return input;
}
int Application::run() {
    using clock = std::chrono::steady_clock;
    constexpr auto step = std::chrono::duration<double>(1.0 / 60.0);
    auto previous = clock::now();
    std::chrono::duration<double> accumulator{};
    int frames{};
    bool running = true;
    while (running && (options_.frame_limit <= 0 || frames < options_.frame_limit)) {
        const auto input = poll_input();
        running = !input.quit;
        const auto now = clock::now();
        accumulator += options_.headless ? step : std::min(now - previous, std::chrono::duration_cast<clock::duration>(std::chrono::milliseconds(250)));
        previous = now;
        bool first = true;
        while (accumulator >= step) {
            scenes_->update(first ? input : InputState{}, static_cast<float>(step.count()));
            accumulator -= step;
            first = false;
        }
        if (!options_.capture_path.empty() && options_.frame_limit > 0 && frames + 1 == options_.frame_limit)
            render_->request_capture(options_.capture_path);
        scenes_->draw();
        ++frames;
        if (!options_.headless) SDL_Delay(1);
    }
    return 0;
}

} // namespace sagas
