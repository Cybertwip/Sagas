// Native integration check: exercise selection input and capture selected poses.
// Run manually with a graphics session; deliberately excluded from ctest.
#include <sagas/Engine.hpp>
#include <sagas/SceneResources.hpp>
#include <SDL3/SDL.h>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>

int main(int argc,char** argv) {
    if (argc!=2) { std::cerr<<"Usage: sagas_selection_capture OUTPUT_DIRECTORY\n"; return 2; }
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    auto* window=SDL_CreateWindow("Selection regression",1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    assert(window);
    const std::filesystem::path output=argv[1];
    std::filesystem::create_directories(output);
    {
        sagas::AssetRepository assets(SAGAS_DEFAULT_ASSET_ROOT);
        sagas::RenderEngine render(window,assets);
        sagas::AudioEngine audio(assets);
        sagas::PhysicsWorld physics;
        sagas::SceneResourceManager resources(assets);
        sagas::Services services{assets,render,audio,physics,resources,true};
        // Mouse pickup must invalidate readiness; release places the puck and
        // moving away must leave the deposited selection ready to start.
        {
            auto scene=sagas::make_character_select_scene(3,false,true);scene->enter(services);
            sagas::InputState click;click.accept_pressed=true;scene->update(services,click,1.f/60);
            sagas::InputState pickup;pickup.pointer_pressed=pickup.pointer_moved=true;pickup.pointer_x=92;pickup.pointer_y=58;
            scene->update(services,pickup,1.f/60);
            sagas::InputState start;start.start_pressed=true;scene->update(services,start,1.f/60);assert(!scene->next());
            sagas::InputState drop;drop.pointer_released=drop.pointer_moved=true;drop.pointer_x=137;drop.pointer_y=101;
            scene->update(services,drop,1.f/60);
            sagas::InputState move;move.pointer_moved=true;move.pointer_x=260;move.pointer_y=160;
            scene->update(services,move,1.f/60);
            render.request_capture(output/"mouse-placed.png");scene->draw(services);
            scene->update(services,start,1.f/60);assert(scene->next());
        }
        for (unsigned fighter=0;fighter<12;++fighter) {
            auto scene=sagas::make_character_select_scene(3,false,true);
            scene->enter(services);
            float x=92,y=58;
            const float target_x=47+45*(fighter%6),target_y=58+43*(fighter/6);
            while (std::abs(x-target_x)>.01f || std::abs(y-target_y)>.01f) {
                const float dx=std::clamp(target_x-x,-4.f,4.f),dy=std::clamp(target_y-y,-4.f,4.f);
                sagas::InputState move; move.stick_x=dx*20; move.stick_y=-dy*20;
                scene->update(services,move,1.f/60); x+=dx; y+=dy;
            }
            sagas::InputState select; select.accept_pressed=true;
            scene->update(services,select,1.f/60);
            for (int frame=0;frame<=120;++frame) {
                if (frame==0 || frame==30 || frame==120) {
                    render.request_capture(output/(std::string(sagas::fighter_kind_name(static_cast<sagas::FighterKind>(fighter)))+
                                                   "-"+std::to_string(frame)+".png"));
                    scene->draw(services);
                }
                scene->update(services,{},1.f/60);
            }
            sagas::InputState start; start.start_pressed=true;
            scene->update(services,start,1.f/60);
            auto battle=scene->next();
            assert(battle); // Chosen roster can proceed to a match.
            battle->enter(services);
            for (int frame=0;frame<180;++frame) {
                sagas::InputState input;
                input.jump_pressed=frame==20 || frame==40 || frame==70;
                input.attack_pressed=frame==45 || frame==75 || frame==120 || frame==125 || frame==135;
                input.grab_pressed=frame==160;
                battle->update(services,input,1.f/60);
                if (frame==45 || frame==75 || frame==150) {
                    render.request_capture(output/(std::string(sagas::fighter_kind_name(static_cast<sagas::FighterKind>(fighter)))+
                                                   "-battle-"+std::to_string(frame)+".png"));
                    battle->draw(services);
                }
            }
            audio.stop();
        }
    }
    SDL_DestroyWindow(window); SDL_Quit();
    // Exercise the actual SDL controls panel, saving and reloading a mapping.
    const auto controls_file=output/"controls.cfg";
    {
        sagas::ApplicationOptions options;options.headless=true;options.start_at_select=true;
        options.frame_limit=1;options.capture_path=output/"controls.png";options.controls_path=controls_file;
        sagas::Application app(options);
        const auto press=[](SDL_Keycode key,SDL_Scancode scan) {
            SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=key;event.key.scancode=scan;SDL_PushEvent(&event);
        };
        press(SDLK_F2,SDL_SCANCODE_F2);press(SDLK_RETURN,SDL_SCANCODE_RETURN);press(SDLK_V,SDL_SCANCODE_V);
        for (int i=0;i<8;++i) press(SDLK_DOWN,SDL_SCANCODE_DOWN);
        press(SDLK_RETURN,SDL_SCANCODE_RETURN);
        assert(app.run()==0);
    }
    std::ifstream saved(controls_file);const std::string config((std::istreambuf_iterator<char>(saved)),{});
    assert(config.find("tap_jump 0")!=std::string::npos);
    assert(config.find("bind 0 "+std::to_string(SDL_SCANCODE_V)+" ")!=std::string::npos);
    {
        sagas::ApplicationOptions options;options.headless=true;options.start_at_select=true;
        options.frame_limit=1;options.capture_path=output/"controls-reloaded.png";options.controls_path=controls_file;
        sagas::Application app(options);
        SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_F2;event.key.scancode=SDL_SCANCODE_F2;SDL_PushEvent(&event);
        assert(app.run()==0);
    }
    std::cout<<"Captured selected poses and scripted jump/jab battle input for all 12 fighters\n";
}
