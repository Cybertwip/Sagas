#include <sagas/Application.hpp>
#include <sagas/Fighter.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <cctype>

namespace sagas {
namespace {
[[noreturn]] void fail(std::string message) {
    if (const char* detail = SDL_GetError(); detail && *detail) message += ": " + std::string(detail);
    throw std::runtime_error(std::move(message));
}
}

void Application::load_controls() {
    bindings_={{{SDL_SCANCODE_J,SDL_GAMEPAD_BUTTON_SOUTH},{SDL_SCANCODE_I,SDL_GAMEPAD_BUTTON_NORTH},
                {SDL_SCANCODE_LSHIFT,-1},{SDL_SCANCODE_E,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
                {SDL_SCANCODE_W,SDL_GAMEPAD_BUTTON_DPAD_UP},{SDL_SCANCODE_S,SDL_GAMEPAD_BUTTON_DPAD_DOWN},
                {SDL_SCANCODE_A,SDL_GAMEPAD_BUTTON_DPAD_LEFT},{SDL_SCANCODE_D,SDL_GAMEPAD_BUTTON_DPAD_RIGHT},{SDL_SCANCODE_K,SDL_GAMEPAD_BUTTON_EAST},{SDL_SCANCODE_Q,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}}};
    std::ifstream file(options_.controls_path);
    std::string line;
    while (std::getline(file,line)) {
        std::istringstream row(line);std::string name;row>>name;
        if (name=="tap_jump") {int value;if (row>>value) tap_jump_=value!=0;continue;}
        int index,key,button;
        if (name=="bind" && row>>index>>key>>button && index>=0 && index<10 && key>0 && key<SDL_SCANCODE_COUNT && button>=-1 && button<SDL_GAMEPAD_BUTTON_COUNT)
            bindings_[index]={key,button};
    }
}
void Application::save_controls() {
    std::ofstream file(options_.controls_path);
    if (!file) {controls_error_="COULD NOT SAVE CONTROLS";return;}
    file<<"# Sagas controls: attack jump shield grab up down left right special taunt\n";
    file<<"tap_jump "<<tap_jump_<<'\n';
    for (unsigned i=0;i<bindings_.size();++i) file<<"bind "<<i<<' '<<bindings_[i].key<<' '<<bindings_[i].button<<'\n';
    file.flush();controls_error_=file?"":"COULD NOT SAVE CONTROLS";
}
void Application::draw_controls() {
    auto& r=*render_;r.begin({16,18,30,255});
    const auto text=[&](std::string value,float x,float y,float scale=1.f) {
        for (unsigned char c:value) {
            c=static_cast<unsigned char>(std::toupper(c));
            if (c>='A' && c<='Z') r.sprite_rect("textures/IFCommonAnnounceCommon/Letter"+std::string(1,c)+".png",x,y,4*scale,7*scale);
            else if (c>='0' && c<='9') r.sprite_rect("textures/IFCommonPlayerDamage/Digit"+std::string(1,c)+".png",x,y,4*scale,7*scale);
            x+=5*scale;
        }
    };
    text("CONTROLS",20,14,1.6f);
    text("ACTION",20,35);text("KEYBOARD",100,35);text("CONTROLLER",178,35);
    constexpr std::array<const char*,10> names{"ATTACK","JUMP","SHIELD","GRAB","UP","DOWN","LEFT","RIGHT","SPECIAL","TAUNT"};
    for (int i=0;i<11;++i) {
        const float y=49+i*13.f;
        if (i==control_row_) r.fill(14,y-3,292,13,{80,40,80,255});
        text(i==10?"TAP JUMP":names[i],20,y);
        if (i==10) text(tap_jump_?"ON":"OFF",100,y);
        else {
            text(SDL_GetScancodeName(static_cast<SDL_Scancode>(bindings_[i].key)),100,y);
            const auto* name=SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(bindings_[i].button));
            text(name?name:(i==2?"TRIGGERS":"NONE"),178,y);
        }
    }
    text(binding_wait_?"PRESS A KEY OR CONTROLLER BUTTON":"UP DOWN SELECT   ENTER CHANGE",20,202);
    text(controls_error_.empty()?"ESC BACK   F2 CONTROLS":controls_error_,20,218);
    r.end();
}

Application::Application(ApplicationOptions options) : options_(std::move(options)) {
    if (options_.headless) {
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) fail("SDL initialization failed");
    load_controls();
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
    auto first_scene = options_.start_at_battle ? make_battle_scene(FighterKind::Mario,FighterKind::Fox) :
                       options_.start_at_select ? make_character_select_scene() : options_.start_at_menu ? make_menu_scene() :
                       (options_.start_at_title ? make_title_scene() : make_startup_scene());
    scenes_ = std::make_unique<SceneMachine>(std::move(first_scene), *services_);
}
Application::~Application() {
    scenes_.reset(); services_.reset(); resources_.reset(); audio_.reset(); render_.reset(); assets_.reset();
    if (gamepad_) SDL_CloseGamepad(gamepad_);
    for (auto* pad:extra_gamepads_) if (pad) SDL_CloseGamepad(pad);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}
InputState Application::poll_input() {
    InputState input;
    input.tap_jump=tap_jump_;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) input.quit = true;
        if (event.type==SDL_EVENT_GAMEPAD_ADDED) {
            if (!gamepad_) gamepad_=SDL_OpenGamepad(event.gdevice.which);
            else for (auto& pad:extra_gamepads_) if (!pad) {pad=SDL_OpenGamepad(event.gdevice.which);break;}
        }
        if (event.type==SDL_EVENT_GAMEPAD_REMOVED) {
            if (gamepad_ && SDL_GetGamepadID(gamepad_)==event.gdevice.which) {SDL_CloseGamepad(gamepad_);gamepad_=nullptr;}
            for (auto& pad:extra_gamepads_) if (pad && SDL_GetGamepadID(pad)==event.gdevice.which) {SDL_CloseGamepad(pad);pad=nullptr;}
        }
        if (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type==SDL_EVENT_GAMEPAD_BUTTON_UP) {
            bool secondary=false;
            for (unsigned slot=0;slot<extra_gamepads_.size();++slot) if (extra_gamepads_[slot] && SDL_GetGamepadID(extra_gamepads_[slot])==event.gbutton.which) {
                secondary=true;auto& c=input.controllers[slot];const bool down=event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                const auto button=event.gbutton.button;
                c.attack|=down && button==bindings_[0].button;c.attack_released|=!down && button==bindings_[0].button;
                c.jump|=down && button==bindings_[1].button;c.jump_released|=!down && button==bindings_[1].button;
                c.special|=down && button==bindings_[8].button;c.grab|=down && button==bindings_[3].button;
                c.shield_pressed|=down && button==bindings_[2].button;c.taunt|=down && button==bindings_[9].button;
                c.cancel|=down && button==SDL_GAMEPAD_BUTTON_EAST;c.start|=down && button==SDL_GAMEPAD_BUTTON_START;
            }
            if (secondary) continue;
        }
        if (event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key==SDLK_F2) {
            controls_open_=!controls_open_;binding_wait_=false;continue;
        }
        if (controls_open_) {
            const bool key=event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat;
            const bool button=event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            if (key && event.key.key==SDLK_ESCAPE) {
                if (binding_wait_) binding_wait_=false;
                else controls_open_=false;
            } else if (binding_wait_ && (key || button)) {
                if (key) bindings_[control_row_].key=event.key.scancode;
                else bindings_[control_row_].button=event.gbutton.button;
                binding_wait_=false;save_controls();
            } else if ((key && event.key.key==SDLK_UP) || (button && event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_UP)) control_row_=(control_row_+10)%11;
            else if ((key && event.key.key==SDLK_DOWN) || (button && event.gbutton.button==SDL_GAMEPAD_BUTTON_DPAD_DOWN)) control_row_=(control_row_+1)%11;
            else if ((key && event.key.key==SDLK_RETURN) || (button && event.gbutton.button==SDL_GAMEPAD_BUTTON_SOUTH)) {
                if (control_row_==10) {tap_jump_=!tap_jump_;save_controls();}
                else binding_wait_=true;
            }
            continue;
        }
        if (event.type==SDL_EVENT_MOUSE_MOTION || event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP) {
            int width,height;SDL_GetWindowSize(window_,&width,&height);
            float x,y;SDL_GetMouseState(&x,&y);
            input.pointer_x=x*320.f/std::max(1,width);input.pointer_y=y*240.f/std::max(1,height);
            input.pointer_moved=true;
            if (event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT) input.pointer_pressed=true;
            if (event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_LEFT) input.pointer_released=true;
        }
        // Map event edges as well as held state: a quick press/release between
        // fixed ticks must not disappear.
        for (unsigned i=0;i<bindings_.size();++i) {
            const auto binding=bindings_[i];
            const bool down=(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode==binding.key) ||
                            (event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button==binding.button);
            const bool up=(event.type==SDL_EVENT_KEY_UP && event.key.scancode==binding.key) ||
                          (event.type==SDL_EVENT_GAMEPAD_BUTTON_UP && event.gbutton.button==binding.button);
            if (i==0) {input.attack_pressed|=down;input.attack_released|=up;}
            if (i==1) {input.jump_pressed|=down;input.jump_released|=up;}
            if (i==2) input.shield_pressed|=down;
            if (i==3) input.grab_pressed|=down;
            if (i==8) input.special_pressed|=down;
            if (i==9) input.taunt_pressed|=down;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            input.accept_pressed |= event.key.key == SDLK_SPACE || event.key.key == SDLK_A;
            input.start_pressed |= event.key.key == SDLK_RETURN;
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
            input.cancel_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
            input.up_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP;
            input.down_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            input.left_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            input.right_pressed |= event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        }
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        input.up |= keys[SDL_SCANCODE_UP];
        input.down |= keys[SDL_SCANCODE_DOWN];
        input.left |= keys[SDL_SCANCODE_LEFT];
        input.right |= keys[SDL_SCANCODE_RIGHT];
    }
    if (gamepad_) {
        input.up |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_UP);
        input.down |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        input.left |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        input.right |= SDL_GetGamepadButton(gamepad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        const float axis_x = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTX) / 409.0f;
        const float axis_y = SDL_GetGamepadAxis(gamepad_, SDL_GAMEPAD_AXIS_LEFTY) / -409.0f;
        if (std::abs(axis_x) >= 8.0f) input.stick_x = std::clamp(axis_x, -80.0f, 80.0f);
        if (std::abs(axis_y) >= 8.0f) input.stick_y = std::clamp(axis_y, -80.0f, 80.0f);
    }
    std::array<bool,10> held{};
    for (unsigned i=0;i<bindings_.size();++i) {
        held[i]=(keys && keys[bindings_[i].key]) || (gamepad_ && bindings_[i].button>=0 &&
            SDL_GetGamepadButton(gamepad_,static_cast<SDL_GamepadButton>(bindings_[i].button)));
    }
    input.shield_held=held[2];
    for (unsigned slot=0;slot<4;++slot) {
        auto* pad=slot==0?gamepad_:extra_gamepads_[slot-1];
        if (!pad) {trigger_held_[slot]=c_jump_held_[slot]=false;continue;}
        const bool trigger=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)>8000 || SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)>8000;
        const bool c_jump=SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_WEST) || std::abs(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTX))>12000 || std::abs(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHTY))>12000;
        if (!slot) {input.shield_held|=trigger;input.shield_pressed|=trigger && !trigger_held_[slot];input.jump_pressed|=c_jump && !c_jump_held_[slot];input.jump_released|=!c_jump && c_jump_held_[slot];}
        else {
            auto& c=input.controllers[slot-1];c.connected=true;c.shield=trigger || (bindings_[2].button>=0 && SDL_GetGamepadButton(pad,static_cast<SDL_GamepadButton>(bindings_[2].button)));
            c.shield_pressed|=trigger && !trigger_held_[slot];c.jump|=c_jump && !c_jump_held_[slot];c.jump_released|=!c_jump && c_jump_held_[slot];
            c.x=std::clamp(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX)/409.f,-80.f,80.f);c.y=std::clamp(SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY)/-409.f,-80.f,80.f);
            if (std::abs(c.x)<8) c.x=80.f*(SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT)-SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_LEFT));
            if (std::abs(c.y)<8) c.y=80.f*(SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_UP)-SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        }
        trigger_held_[slot]=trigger;c_jump_held_[slot]=c_jump;
    }
    input.pointer_held=(SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_LMASK)!=0;
    input.up=held[4];input.down=held[5];input.left=held[6];input.right=held[7];
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
    InputState pending;
    while (running && (options_.frame_limit <= 0 || frames < options_.frame_limit)) {
        auto input = poll_input();
        input.latch_edges(pending);
        pending=input;
        running = !input.quit;
        const auto now = clock::now();
        accumulator += options_.headless ? step : now - previous;
        previous = now;
        while (accumulator >= step) {
            if (!controls_open_) scenes_->update(pending, static_cast<float>(step.count()));
            pending.clear_edges();
            accumulator -= step;
        }
        if (!options_.capture_path.empty() && options_.frame_limit > 0 && frames + 1 == options_.frame_limit)
            render_->request_capture(options_.capture_path);
        if (!options_.headless || !options_.capture_only || frames<60 || frames+1==options_.frame_limit)
            {if (controls_open_) draw_controls();else scenes_->draw();}
        ++frames;
        if (!options_.headless) SDL_Delay(1);
    }
    return 0;
}

} // namespace sagas
