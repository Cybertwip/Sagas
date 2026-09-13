// Native visual smoke tests for callback-driven battle moves.
#include <sagas/Engine.hpp>
#include <sagas/SceneResources.hpp>
#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <cmath>
using namespace sagas;
int main(int argc,char** argv) {
    if(argc<2)return 2;
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    auto* window=SDL_CreateWindow("Battle regressions",1280,720,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(window);
    const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
    {
        AssetRepository assets(SAGAS_DEFAULT_ASSET_ROOT);RenderEngine render(window,assets);AudioEngine audio(assets);
        PhysicsWorld physics;SceneResourceManager resources(assets);Services services{assets,render,audio,physics,resources,true};
        for(auto kind:{FighterKind::Luigi,FighterKind::Mario,FighterKind::Ness,FighterKind::Fox,FighterKind::Samus,FighterKind::Link,FighterKind::Yoshi,FighterKind::Kirby,FighterKind::Captain,FighterKind::Pikachu,FighterKind::Purin}) {
            if(argc>2 && fighter_kind_name(kind)!=argv[2])continue;
            for(int move=0;move<4;++move) {
                if(argc>3 && move!=std::stoi(argv[3]))continue;
                auto battle=make_battle_scene(std::vector<FighterKind>{kind,FighterKind::Mario},3,{0,1});battle->enter(services);
                bool saw_projectile=false,saw_launch=false,saw_explosion=false,saw_bomb_throw=false;float start_y=0,max_y=-100000;
                for(int frame=0;frame<460;++frame) {
                    InputState input;input.controllers[0].connected=true;
                    if(frame==75 && move==2 && kind==FighterKind::Link)input.jump_pressed=true;

                    if(frame==90) {if(move==0)input.grab_pressed=true;else {input.special_pressed=true;input.special_held=true;input.stick_y=move==2?80:move==3?-80:0;}}
                    if(frame>90 && move>0)input.special_held=true;
                    if(frame==150 && move==1 && kind==FighterKind::Samus)input.special_pressed=true;
                    if(move==3 && kind==FighterKind::Link) {
                        if(argc>4 && frame==135)input.jump_pressed=true;
                        if(frame==150) {input.attack_pressed=true;input.stick_x=argc>4?std::stoi(argv[4])*80:80;}
                    }
                    if(move==3 && (kind==FighterKind::Mario || kind==FighterKind::Luigi) && frame>90 && frame<140)
                        input.special_pressed=frame%3==0;
                    if(frame==119 && move==0)input.attack_pressed=true;
                    if(frame>145 && kind==FighterKind::Ness && move==2) {
                        for(const auto& shot:battle_projectiles(*battle))if(shot.weapon==10) {
                            const auto& ness=battle_fighters(*battle)[0];
                            const float dx=ness.position.x-shot.position.x,dy=ness.position.y+150-shot.position.y;
                            const float length=std::hypot(dx,dy);input.stick_x=80*dx/length;input.stick_y=80*dy/length;
                        }
                    }
                    if(frame==89)start_y=battle_fighters(*battle)[0].position.y;
                    battle->update(services,input,1.f/60);
                    for(const auto& shot:battle_projectiles(*battle)) {
                        if(shot.weapon==14 && !shot.held && !shot.exploding) {
                            saw_bomb_throw=true;
                            if(frame==150) {
                                assert(shot.velocity.x*(argc>4?std::stoi(argv[4]):1)>0);
                                if(argc>4)assert(!battle_fighters(*battle)[0].grounded);
                            }
                        }
                        if(shot.weapon==15 && !saw_projectile && kind==FighterKind::Yoshi && move==3) {
                            saw_projectile=true;render.request_capture(out/"Yoshi-stars.png");battle->draw(services);
                        }
                        if(shot.exploding && !saw_explosion) {
                            saw_explosion=true;render.request_capture(out/(std::string(fighter_kind_name(kind))+"-explosion.png"));battle->draw(services);
                        }
                    }
                    if(frame>=90)max_y=std::max(max_y,battle_fighters(*battle)[0].position.y);
                    saw_launch|=battle_fighters(*battle)[0].special_phase==3;
                    if(frame==90)assert(battle_fighters(*battle)[0].status==(move==0?FighterStatus::Catch:FighterStatus::Special));
                    if(kind==FighterKind::Kirby && move==2)saw_projectile|=battle_projectile_count(*battle,9)>0;
                    if(kind==FighterKind::Pikachu && move==3)saw_projectile|=battle_projectile_count(*battle,7)>0;
                    if(kind==FighterKind::Ness && move==2)saw_projectile|=battle_projectile_count(*battle,10)>0;
                    if(kind==FighterKind::Fox && move==1)saw_projectile|=battle_projectile_count(*battle,2)>0;
                    if((kind==FighterKind::Mario || kind==FighterKind::Luigi) && move==1)saw_projectile|=battle_projectile_count(*battle,kind==FighterKind::Luigi?0:1)>0;
                    if(kind==FighterKind::Samus && move==1)saw_projectile|=battle_projectile_count(*battle,3)>0;
                    if(kind==FighterKind::Link && move==1)saw_projectile|=battle_projectile_count(*battle,4)>0;
                    if(kind==FighterKind::Yoshi && move==2)saw_projectile|=battle_projectile_count(*battle,12)>0;
                    if(kind==FighterKind::Samus && move==3)saw_projectile|=battle_projectile_count(*battle,13)>0;
                    if(kind==FighterKind::Link && move==3)saw_projectile|=battle_projectile_count(*battle,14)>0;
                    if(frame==96 || frame==110 || frame==120 || frame==135 || frame==155) {
                        render.request_capture(out/(std::string(fighter_kind_name(kind))+"-"+std::to_string(move)+"-"+std::to_string(frame-90)+".png"));
                        battle->draw(services);
                    }
                }
                if((kind==FighterKind::Kirby && move==2) || (kind==FighterKind::Pikachu && move==3) || (kind==FighterKind::Fox && move==1) || (kind==FighterKind::Ness && move==2))assert(saw_projectile);
                if(move==2 && (kind==FighterKind::Captain || kind==FighterKind::Mario || kind==FighterKind::Luigi || kind==FighterKind::Kirby))assert(max_y>start_y+300);
                if(move==2 && kind==FighterKind::Ness)assert(saw_launch);
                if(move==2 && kind==FighterKind::Link)assert(max_y>start_y+300);
                if((move==3 && (kind==FighterKind::Samus || kind==FighterKind::Link)) || (move==2 && kind==FighterKind::Yoshi))assert(saw_explosion);
                if(move==3 && kind==FighterKind::Link)assert(saw_bomb_throw);
                if(move==3 && kind==FighterKind::Yoshi) {assert(saw_projectile);assert(max_y>start_y+300);}
                if((move==1 && (kind==FighterKind::Mario || kind==FighterKind::Luigi || kind==FighterKind::Samus || kind==FighterKind::Link)) || (move==2 && kind==FighterKind::Yoshi) || (move==3 && (kind==FighterKind::Link || kind==FighterKind::Samus)))assert(saw_projectile);
                if(move==3 && (kind==FighterKind::Mario || kind==FighterKind::Luigi))assert(max_y>start_y+100);
                std::cout<<fighter_kind_name(kind)<<" "<<move<<std::endl;
            }
        }
    }
    SDL_DestroyWindow(window);SDL_Quit();
}
