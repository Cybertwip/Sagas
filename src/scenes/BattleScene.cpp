#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/FighterSourceData.hpp>
#include <sagas/FighterAttackData.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>
#include <sagas/OpeningMotionAudio.hpp>
#include <sagas/BattleMotionAudio.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace sagas {
namespace {
class BattleScene final : public Scene {
public:
    BattleScene(std::vector<FighterKind> fighters,int stock):stock_(stock) {
        for (const auto kind:fighters) {
            FighterBody body; body.kind=kind; body.attr=fighter_attributes(kind); body.stocks=stock;
            bodies_.push_back(body);
        }
    }
    void enter(Services& services) override {
        loader_=std::make_unique<Scene3DLoader>(services.resources.archive());
        renderer_=std::make_unique<Scene3DRenderer>(services.resources.archive());
        archive_=&services.resources.archive();
        stage_=loader_->stage("llGRPupupuMapMapHeader");
        for (unsigned i=0;i<bodies_.size();++i) {
            auto& body=bodies_[i]; body.position=stage_.player_spawns[i];
            body.grounded=false; body.lr=body.position.x<0?1:-1;
            body.status=FighterStatus::Fall;
        }
        services.audio.stop();
        camera_.tick(bodies_,stage_);
    }
    void update(Services& services,const InputState& input,float) override {
        ++tic_;
        if (input.back_pressed) done_=true;
        if (winner_>=0) { if (++finish_tics_>180 || input.start_pressed) done_=true; return; }
        for (unsigned i=0;i<bodies_.size();++i) {
            auto& body=bodies_[i];
            if (body.stocks<=0) continue;
            const int old_x=body.stick_x,old_y=body.stick_y;
            bool attack=false;
            if (i==0) {
                body.stick_x=static_cast<int>(input.stick_x); body.stick_y=static_cast<int>(input.stick_y);
                body.jump_pressed=input.jump_pressed || (body.stick_y>=53 && previous_stick_y_<53);
                if (body.jump_pressed) body.jump_button=input.jump_pressed;
                body.jump_released=input.jump_released;
                body.shield_held=input.shield_held; attack=input.accept_pressed;
                previous_stick_y_=body.stick_y;
            } else {
                // Deterministic CPU approach; the same body/attack/collision
                // path is used for humans and CPU fighters.
                const auto target=std::find_if(bodies_.begin(),bodies_.end(),[&](const auto& other){return &other!=&body && other.stocks>0;});
                const float dx=target!=bodies_.end()?target->position.x-body.position.x:0;
                const float dy=target!=bodies_.end()?target->position.y-body.position.y:0;
                body.stick_x=std::abs(dx)>260?(dx>0?60:-60):0; body.stick_y=0;
                if (std::abs(dx)>1 && body.status!=FighterStatus::Attack) body.lr=dx>0?1:-1;
                body.jump_pressed=body.grounded && dy>300 && tic_%40==0;
                body.jump_button=true; body.jump_released=false;
                attack=std::abs(dx)<420 && tic_%32==static_cast<int>(i)*3;
            }
            body.tap_stick_x=std::abs(body.stick_x)>=56 && (std::abs(old_x)<56 || old_x*body.stick_x<0)?0:std::min(255,body.tap_stick_x+1);
            body.tap_stick_y=std::abs(body.stick_y)>=53 && (std::abs(old_y)<53 || old_y*body.stick_y<0)?0:std::min(255,body.tap_stick_y+1);
            const bool on_cliff=body.status==FighterStatus::CliffCatch || body.status==FighterStatus::CliffWait || body.status==FighterStatus::CliffClimb;
            if (on_cliff) update_cliff(body);
            if (!on_cliff && !body.hitlag) {
                ++body.action_frame;
                if (body.status==FighterStatus::Hitstun && --body.hitstun<=0)
                    body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;
                const bool smash=FighterCombat::start_smash(body,attack);
                const bool ended=(body.status==FighterStatus::Attack || body.status==FighterStatus::Jump || body.status==FighterStatus::Dash) &&
                                  body.action_frame>=motion_length(body);
                FighterCombat::advance_jab(body,attack && !smash,ended,i==0 && input.attack_released);
                if (body.status==FighterStatus::Jump && ended) body.status=FighterStatus::Fall;
                if (body.status==FighterStatus::Dash && ended) {body.status=FighterStatus::Wait;body.vel_ground*=.75f;}
                if (body.status!=FighterStatus::Hitstun && body.status!=FighterStatus::Attack) {
                    if (body.shield_held && body.grounded && body.shield>0) {
                        body.status=FighterStatus::Shield; body.shield=std::max(0.0f,body.shield-.15f);
                    } else if (body.status==FighterStatus::Shield) body.status=FighterStatus::Wait;
                }
                if (body.status!=FighterStatus::Shield) body.shield=std::min(55.0f,body.shield+.05f);
            }
            const auto before=body.position;
            if (!on_cliff) FighterPhysics::tick(body,stage_.collision,[&](const FighterBody& jumping) -> std::optional<Vec3> {
                const auto model=posed(jumping,true);
                if (!model.fighter_root_animation) return {};
                n64::AnimationDecoder decoder(*archive_);
                float ended=-1;
                const auto previous=decoder.sample16(*model.fighter_root_animation,jumping.jump_frames,
                                                     decoder.pose(model.fighter_root),&ended);
                if (ended>=0) return {};
                const auto current=decoder.sample16(*model.fighter_root_animation,jumping.jump_frames+1,
                                                    decoder.pose(model.fighter_root));
                const float z=(current.tracks[6]-previous.tracks[6])*jumping.lr*jumping.attr.size;
                const float y=(current.tracks[5]-previous.tracks[5])*jumping.attr.size;
                const float angle=current.tracks[2];
                return Vec3{z*std::cos(angle)-y*std::sin(angle),z*std::sin(angle)+y*std::cos(angle),0};
            });
            if (!on_cliff && FighterPhysics::try_ledge(body,before,stage_.collision,bodies_)) update_cliff(body,false);
            const auto& bounds=stage_.blast_bounds;
            if (body.position.x<bounds[3] || body.position.x>bounds[2] || body.position.y<bounds[1] || body.position.y>bounds[0]) {
                --body.stocks;
                if (!body.stocks) {body.status=FighterStatus::KO;continue;}
                body.position={0,1500,0}; body.vel_air={}; body.vel_damage={}; body.damage=0; body.hitstun=body.hitlag=0;
                body.grounded=false; body.status=FighterStatus::Fall; body.invincible=180;
            }
            const unsigned clip=motion(body);
            if (body.motion!=clip) {body.motion=clip;body.action_frame=0;}
            if (!body.hitlag && !services.deterministic_clock)
                for (const auto& sound:battle_motion_sounds)
                    if (sound.motion==clip && sound.frame==static_cast<unsigned>(body.action_frame)) {
                        const auto& voices=fighter_source_data[static_cast<unsigned>(body.kind)].smash_voices;
                        unsigned fgm=sound.fgm==~0U?voices[(tic_+i)%3]:sound.fgm;
                        if (body.kind==FighterKind::Luigi)
                            for (unsigned v=0;v<3;++v)
                                if (fgm==fighter_source_data[static_cast<unsigned>(FighterKind::Mario)].smash_voices[v]) {fgm=voices[v];break;}
                        services.audio.play_fgm(fgm);
                    }
        }
        std::vector<AttackVolume> volumes;
        for (unsigned i=0;i<bodies_.size();++i) {
            auto& body=bodies_[i];
            if (body.status!=FighterStatus::Attack || body.hitlag || body.stocks<=0) continue;
            const auto model=posed(body);
            for (const auto& box:source_jab_hitboxes)
                if (box.motion==body.motion && body.action_frame>=static_cast<int>(box.begin) && body.action_frame<static_cast<int>(box.end)) {
                    if (body.attack_epoch!=box.epoch) {body.attack_epoch=box.epoch;body.hit_mask=0;}
                    const auto position=renderer_->joint_point(model,body.action_frame,box.joint,
                        {static_cast<float>(box.x),static_cast<float>(box.y),static_cast<float>(box.z)});
                    volumes.push_back({i,position,box.radius*body.attr.size,box.damage,box.angle,box.growth,box.weight,box.base,box.fgm});
                }
        }
        const auto hits=FighterCombat::resolve(bodies_,volumes);
        if (!services.deterministic_clock)
            for (const auto& hit:hits) if (!hit.shield) services.audio.play_fgm(hit.fgm);
        int alive=0;
        for (unsigned i=0;i<bodies_.size();++i) if (bodies_[i].stocks>0) {++alive;winner_=static_cast<int>(i);}
        if (alive>1) winner_=-1;
        camera_.tick(bodies_,stage_);
    }
    void draw(Services& services) override {
        auto& r=services.render; r.begin({100,150,220,255});
        r.sprite_rect("textures/StageDreamLand.png",0,0,320,240);
        renderer_->begin();
        const auto& camera=camera_.view();
        for (const auto& layer:stage_.layers) renderer_->draw(r,layer,camera,static_cast<float>(tic_));
        for (const auto& body:bodies_) {
            if (body.stocks<=0 || (body.invincible && tic_%6<2)) continue;
            const auto model=posed(body);
            renderer_->draw(r,model,camera,static_cast<float>(body.action_frame),
                            body.status==FighterStatus::Shield?Color{130,160,255,255}:Color{255,255,255,255});
        }
        renderer_->end(r);
        for (unsigned i=0;i<bodies_.size();++i) {
            const float x=12+i*76.0f;
            r.fill(x,192,72,42,{0,0,0,150});
            r.sprite_at("textures/MNPlayersPortraits/"+std::string(fighter_portrait_file(bodies_[i].kind)),{x,194},{.45f,.45f});
            const int damage=std::min(999,static_cast<int>(bodies_[i].damage));
            for (int digit=0;digit<3;++digit) {
                const int divisor=digit==0?100:digit==1?10:1;
                if (digit==0 && damage<100) continue;
                r.sprite_at("textures/IFCommonPlayerDamage/Digit"+std::to_string(damage/divisor%10)+".png",{x+27+digit*10.0f,206},{.8f,.8f});
            }
            r.sprite_at("textures/IFCommonPlayerDamage/SymbolPercent.png",{x+59,210},{.8f,.8f});
            for (int stock=0;stock<bodies_[i].stocks;++stock) r.fill(x+4+stock*6,226,4,4,i==0?Color{255,80,80,255}:Color{100,170,255,255});
        }
        r.end();
    }
    std::unique_ptr<Scene> next() override {return done_?make_character_select_scene(stock_,false):nullptr;}
private:
    void update_cliff(FighterBody& body,bool advance=true) {
        if (body.hitlag) {--body.hitlag;return;}
        if (advance) {++body.action_frame;if (body.invincible>0) --body.invincible;}
        if (body.status==FighterStatus::CliffCatch && body.action_frame>=motion_length(body)) {
            body.status=FighterStatus::CliffWait;body.action_frame=0;body.invincible=60;
            body.cliff_wait=body.damage<100?1080:480;
        } else if (body.status==FighterStatus::CliffClimb && body.action_frame>=motion_length(body)) {
            if (++body.cliff_phase==3) {
                body.status=FighterStatus::Wait;body.grounded=true;body.jumps_used=0;body.invincible=0;body.cliff_cooldown=30;
                return;
            }
            body.action_frame=0;
        }
        if (body.status==FighterStatus::CliffWait) {
            if (std::abs(body.stick_x)<20 && std::abs(body.stick_y)<20) body.cliff_neutral=true;
            const bool input=std::abs(body.stick_x)>=20 || std::abs(body.stick_y)>=20;
            if (--body.cliff_wait<=0 || (body.cliff_neutral && input &&
                (body.stick_y<=-20 || body.stick_x*body.lr<=-20))) {
                body.status=FighterStatus::Fall;body.cliff_cooldown=30;body.invincible=0;
                body.position={body.cliff_edge.x-(body.attr.width+30)*body.lr,body.cliff_edge.y-body.attr.height*.5f,0};
                return;
            }
            if (body.cliff_neutral && input) {body.status=FighterStatus::CliffClimb;body.cliff_phase=0;body.action_frame=0;}
        }
        const auto model=posed(body,true);
        if (model.fighter_root_animation) {
            n64::AnimationDecoder decoder(*archive_);
            const auto pose=decoder.sample16(*model.fighter_root_animation,body.action_frame,decoder.pose(model.fighter_root));
            body.position={body.cliff_edge.x+pose.tracks[6]*body.lr*body.attr.size,
                           body.cliff_edge.y+pose.tracks[5]*body.attr.size,0};
        }
    }
    static unsigned motion(const FighterBody& body) {
        const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
        switch(body.status) {
            case FighterStatus::Walk:return data.walk;
            case FighterStatus::Dash:return data.dash_clip;
            case FighterStatus::Run:return data.run_clip;
            case FighterStatus::RunBrake:return data.run_brake;
            case FighterStatus::CliffCatch:return data.cliff[0];
            case FighterStatus::CliffWait:return data.cliff[1];
            case FighterStatus::CliffClimb:return data.cliff[(body.damage<100?2:5)+body.cliff_phase];
            case FighterStatus::KneeBend:return data.kneebend_clip;
            case FighterStatus::Jump:
                if (!body.aerial_jump) return body.jump_backward?data.jump_back:data.jump;
                if (data.multi_jump[0]) return data.multi_jump[std::clamp(body.jumps_used-2,0,4)];
                return body.jump_backward?data.aerial_back:data.aerial_forward;
            case FighterStatus::Fall:return data.fall;
            case FighterStatus::Land:return data.landing;
            case FighterStatus::Attack:
                if (body.attack_motion) return body.attack_motion;
                if (body.jab_stage>=4) return data.rapid[body.jab_stage-4];
                return body.jab_stage==3?data.jab3:body.jab_stage==2?data.jab2:data.jab;
            case FighterStatus::Hitstun:return data.damage;
            default:return data.idle;
        }
    }
    float motion_length(const FighterBody& body) {
        const unsigned clip=motion(body);
        if (!lengths_.contains(clip)) {
            const auto model=posed(body,true);
            n64::AnimationDecoder decoder(*archive_);
            float length=0;
            auto scripts=model.animation;
            scripts.push_back(model.fighter_root_animation);
            for (const auto& script:scripts) if (script) {
                float end=-1;
                (void)decoder.sample16(*script,10000,{},&end);
                if (end<0) {length=10000;break;}
                length=std::max(length,end);
            }
            lengths_[clip]=length;
        }
        return lengths_.at(clip);
    }
    Model3D posed(const FighterBody& body,bool with_root=false) {
        const unsigned clip=motion(body);
        const unsigned key=static_cast<unsigned>(body.kind)*4096+clip;
        if (!models_.contains(key)) {
            const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
            const unsigned flags=((body.kind==FighterKind::Ness || body.kind==FighterKind::Yoshi) &&
                (clip==data.aerial_forward || clip==data.aerial_back)) ||
                std::find(data.cliff.begin(),data.cliff.end(),clip)!=data.cliff.end()?0x40000000U:0;
            models_.emplace(key,loader_->fighter_motion(body.kind,clip,flags));
        }
        auto model=models_.at(key);
        if (!with_root && model.fighter_wrapper==Model3D::FighterWrapper::TransN) model.fighter_root_animation.reset();
        model.position=body.position;model.rotation.y=body.lr*std::numbers::pi_v<float>/2;
        model.scale={body.attr.size,body.attr.size,body.attr.size};return model;
    }
    int stock_,tic_{},previous_stick_y_{},winner_{-1},finish_tics_{};
    bool done_{};
    std::vector<FighterBody> bodies_;
    Stage3D stage_;
    BattleCamera camera_;
    n64::RelocArchive* archive_{};
    std::unordered_map<unsigned,Model3D> models_;
    std::unordered_map<unsigned,float> lengths_;
    std::unique_ptr<Scene3DLoader> loader_;
    std::unique_ptr<Scene3DRenderer> renderer_;
};
}
std::unique_ptr<Scene> make_battle_scene(std::vector<FighterKind> fighters,int stock) {
    return std::make_unique<BattleScene>(std::move(fighters),stock);
}
std::unique_ptr<Scene> make_battle_scene(FighterKind p1,FighterKind p2,int stock) {
    return make_battle_scene(std::vector<FighterKind>{p1,p2},stock);
}
}
