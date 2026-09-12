// Native visual smoke tests for callback-driven battle moves.
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
    auto* window=SDL_CreateWindow("Battle regressions",1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    {
        AssetRepository assets(SAGAS_DEFAULT_ASSET_ROOT);RenderEngine render(window,assets);AudioEngine audio(assets);
        PhysicsWorld physics;SceneResourceManager resources(assets);Services services{assets,render,audio,physics,resources,true};
        for(auto kind:{FighterKind::Mario,FighterKind::Fox,FighterKind::Samus,FighterKind::Link,FighterKind::Yoshi,FighterKind::Kirby,FighterKind::Captain,FighterKind::Pikachu,FighterKind::Purin}) {
            for(int move=0;move<4;++move) {
                auto battle=make_battle_scene(std::vector<FighterKind>{kind,FighterKind::Mario},3,{0,1});battle->enter(services);
                bool saw_projectile=false;
                for(int frame=0;frame<170;++frame) {
                    InputState input;input.controllers[0].connected=true;

                    if(frame==90) {if(move==0)input.grab_pressed=true;else {input.special_pressed=true;input.special_held=true;input.stick_y=move==2?80:move==3?-80:0;}}
                    if(frame>90 && move>0)input.special_held=true;
                    if(frame==119 && move==0)input.attack_pressed=true;
                    battle->update(services,input,1.f/60);
                    if(frame==90)assert(battle_fighters(*battle)[0].status==(move==0?FighterStatus::Catch:FighterStatus::Special));
                    if(kind==FighterKind::Kirby && move==2)saw_projectile|=battle_projectile_count(*battle,9)>0;
                    if(kind==FighterKind::Pikachu && move==3)saw_projectile|=battle_projectile_count(*battle,7)>0;
                    if(kind==FighterKind::Fox && move==1)saw_projectile|=battle_projectile_count(*battle,2)>0;
                    if(frame==96 || frame==110 || frame==120 || frame==135 || frame==155) {
                        render.request_capture(out/(std::string(fighter_kind_name(kind))+"-"+std::to_string(move)+"-"+std::to_string(frame-90)+".png"));
                        battle->draw(services);
                    }
                }
                if((kind==FighterKind::Kirby && move==2) || (kind==FighterKind::Pikachu && move==3) || (kind==FighterKind::Fox && move==1))assert(saw_projectile);
                std::cout<<fighter_kind_name(kind)<<" "<<move<<std::endl;
            }
        }
    }
    SDL_DestroyWindow(window);SDL_Quit();
}
