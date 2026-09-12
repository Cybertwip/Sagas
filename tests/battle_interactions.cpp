// End-to-end input replay: real battle scene, collision, capture, release and rendering.
#include <sagas/Engine.hpp>
#include <sagas/SceneResources.hpp>
#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <iostream>
using namespace sagas;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    auto* window=SDL_CreateWindow("Battle contact regressions",1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    {
        AssetRepository assets(SAGAS_DEFAULT_ASSET_ROOT);RenderEngine render(window,assets);AudioEngine audio(assets);
        PhysicsWorld physics;SceneResourceManager resources(assets);Services services{assets,render,audio,physics,resources,true};
        for(auto kind:{FighterKind::Mario,FighterKind::Fox,FighterKind::Samus,FighterKind::Link,FighterKind::Yoshi,FighterKind::Kirby}) for(int direction=0;direction<(kind==FighterKind::Kirby?3:2);++direction) {
            auto battle=make_battle_scene(std::vector<FighterKind>{kind,FighterKind::Donkey},3,{0,1});battle->enter(services);
            bool initiated=false,caught=false,released=false,thrown=false;int caught_at=0;
            for(int frame=0;frame<650;++frame) {
                InputState input;input.controllers[0].connected=true;
                const auto before=battle_fighters(*battle);
                const auto& holder=before[0];const auto& target=before[1];
                if(frame==60)input.controllers[0].y=-80;
                if(!initiated && frame>100) {
                    const float dx=target.position.x-holder.position.x;
                    if(std::abs(dx)>250)input.stick_x=dx>0?30:-30;
                    else if(holder.grounded && target.grounded && holder.status==FighterStatus::Wait) {
                        if(direction==2){input.special_pressed=true;input.special_held=true;}else input.grab_pressed=true;
                        initiated=true;
                    }
                }
                if(direction==2 && initiated)input.special_held=true;
                if(caught && !thrown && holder.status==FighterStatus::CatchWait && holder.capture_tics>=1) {
                    if(direction==0)input.attack_pressed=true;else input.stick_x=-holder.lr*80;
                }
                if(caught && direction==2 && frame-caught_at>20)input.attack_pressed=true;
                battle->update(services,input,1.f/60);
                const auto after=battle_fighters(*battle);
                if(after[1].status==FighterStatus::Captured && !caught) {
                    caught=true;caught_at=frame;
                    if(kind==FighterKind::Yoshi || direction==2)assert(after[1].swallowed);
                }
                if(after[0].status==FighterStatus::Throw)thrown=true;
                if(caught && (frame==caught_at+1 || frame==caught_at+15 || (thrown && after[0].action_frame==5))) {
                    render.request_capture(out/(std::string(fighter_kind_name(kind))+"-"+std::to_string(direction)+"-"+std::to_string(frame-caught_at)+".png"));battle->draw(services);
                }
                if(caught && after[1].captured_by<0 && after[1].damage>0) {released=true;assert(!after[1].swallowed);break;}
            }
            std::cout<<fighter_kind_name(kind)<<" direction "<<direction<<" caught "<<caught<<" released "<<released<<std::endl;
            assert(initiated && caught && released);
        }
    }
    SDL_DestroyWindow(window);SDL_Quit();
}
