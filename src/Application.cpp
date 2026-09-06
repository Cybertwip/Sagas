#include <sagas/Application.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
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
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) fail("SDL initialization failed");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE,1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,4);
    const auto flags = SDL_WINDOW_OPENGL | (options_.headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE);
    window_=SDL_CreateWindow("Sagas | Smash Remix",1280,720,flags);
    if (!window_) fail("OpenGL window creation failed");
    if (!SDL_SetWindowAspectRatio(window_,16.0f/9.0f,16.0f/9.0f))
        fail("window aspect ratio setup failed");
    assets_ = std::make_unique<AssetRepository>(options_.asset_root);
    render_ = std::make_unique<RenderEngine>(window_, *assets_);
    audio_ = std::make_unique<AudioEngine>(*assets_);
    resources_ = std::make_unique<SceneResourceManager>(*assets_);
    services_ = std::make_unique<Services>(Services{*assets_, *render_, *audio_, physics_, *resources_, options_.headless});
    auto first_scene = options_.start_at_menu ? make_menu_scene() :
                       (options_.start_at_title ? make_title_scene() : make_startup_scene());
    scenes_ = std::make_unique<SceneMachine>(std::move(first_scene), *services_);
}
Application::~Application() {
    scenes_.reset(); services_.reset(); resources_.reset(); audio_.reset(); render_.reset(); assets_.reset();
    if (gamepad_) SDL_CloseGamepad(gamepad_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}
InputState Application::poll_input() {
    InputState input;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) input.quit = true;
        if (event.type == SDL_EVENT_GAMEPAD_ADDED && !gamepad_)
            gamepad_ = SDL_OpenGamepad(event.gdevice.which);
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED && gamepad_ &&
            event.gdevice.which == SDL_GetGamepadID(gamepad_)) {
            SDL_CloseGamepad(gamepad_);
            gamepad_ = nullptr;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            input.accept_pressed |= event.key.key == SDLK_SPACE || event.key.key == SDLK_A;
            input.start_pressed |= event.key.key == SDLK_RETURN;
            input.jump_pressed |= event.key.key == SDLK_X;
            input.back_pressed |= event.key.key == SDLK_ESCAPE;
            input.cancel_pressed |= event.key.key == SDLK_ESCAPE || event.key.key == SDLK_B;
            input.skip_pressed |= event.key.key == SDLK_S;
            input.up_pressed |= event.key.key == SDLK_UP;
            input.down_pressed |= event.key.key == SDLK_DOWN;
            input.left_pressed |= event.key.key == SDLK_LEFT;
            input.right_pressed |= event.key.key == SDLK_RIGHT;
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            input.accept_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH;
            input.start_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_START;
            input.jump_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH || event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST;
            input.cancel_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
            input.up_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP;
            input.down_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            input.left_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            input.right_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        }
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        input.shield_held |= keys[SDL_SCANCODE_Z];
        input.up |= keys[SDL_SCANCODE_UP];
        input.down |= keys[SDL_SCANCODE_DOWN];
        input.left |= keys[SDL_SCANCODE_LEFT];
        input.right |= keys[SDL_SCANCODE_RIGHT];
    }
    if (gamepad_) {
        input.shield_held |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        input.up |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_UP);
        input.down |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        input.left |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        input.right |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        const float axis_x = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX) / 409.0f;
        const float axis_y = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY) / -409.0f;
        if (std::abs(axis_x) >= 8.0f) input.stick_x = std::clamp(axis_x, -80.0f, 80.0f);
        if (std::abs(axis_y) >= 8.0f) input.stick_y = std::clamp(axis_y, -80.0f, 80.0f);
    }
    if (std::abs(input.stick_x) < 8.0f) input.stick_x = (input.right ? 80.0f : 0.0f) + (input.left ? -80.0f : 0.0f);
    if (std::abs(input.stick_y) < 8.0f) input.stick_y = (input.up ? 80.0f : 0.0f) + (input.down ? -80.0f : 0.0f);
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
        accumulator += options_.headless ? step : now - previous;
        previous = now;
        bool first = true;
        while (accumulator >= step) {
            scenes_->update(first ? input : InputState{}, static_cast<float>(step.count()));
            accumulator -= step;
            first = false;
        }
        if (!options_.capture_path.empty() && options_.frame_limit > 0 && frames + 1 == options_.frame_limit)
            render_->request_capture(options_.capture_path);
        if (!options_.headless || !options_.capture_only || frames<60 || frames+1==options_.frame_limit)
            scenes_->draw();
        ++frames;
        if (!options_.headless) SDL_Delay(1);
    }
    return 0;
}

} // namespace sagas
