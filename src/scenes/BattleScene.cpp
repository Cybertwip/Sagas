#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/FighterSourceData.hpp>
#include <sagas/FighterAttackData.hpp>
#include <sagas/Scene3D.hpp>
#include <sagas/SceneResources.hpp>
#include <sagas/OpeningMotionAudio.hpp>

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
    }
    void update(Services& services,const InputState& input,float) override {
        ++tic_;
        if (input.back_pressed) done_=true;
        if (winner_>=0) { if (++finish_tics_>180 || input.start_pressed) done_=true; return; }
        for (unsigned i=0;i<bodies_.size();++i) {
            auto& body=bodies_[i];
            if (body.stocks<=0) continue;
            bool attack=false;
            if (i==0) {
                body.stick_x=static_cast<int>(input.stick_x); body.stick_y=static_cast<int>(input.stick_y);
                body.jump_pressed=input.jump_pressed || (body.stick_y>=44 && previous_stick_y_<44);
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
                attack=std::abs(dx)<420 && tic_%32==i*3;
            }
            if (!body.hitlag) {
                ++body.action_frame;
                if (body.status==FighterStatus::Hitstun && --body.hitstun<=0)
                    body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;
                if (body.status==FighterStatus::Attack && body.action_frame>=30)
                    body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;
                if (body.status!=FighterStatus::Hitstun && body.status!=FighterStatus::Attack) {
                    if (body.shield_held && body.grounded && body.shield>0) {
                        body.status=FighterStatus::Shield; body.shield=std::max(0.0f,body.shield-.15f);
                    } else if (body.status==FighterStatus::Shield) body.status=FighterStatus::Wait;
                    if (attack && body.grounded) {
                        body.status=FighterStatus::Attack; body.action_frame=0; body.hit_mask=0;
                        body.vel_ground=0;
                    }
                }
                if (body.status!=FighterStatus::Shield) body.shield=std::min(55.0f,body.shield+.05f);
            }
            FighterPhysics::tick(body,stage_.collision);
            const auto& bounds=stage_.blast_bounds;
            if (body.position.x<bounds[3] || body.position.x>bounds[2] || body.position.y<bounds[1] || body.position.y>bounds[0]) {
                --body.stocks;
                if (!body.stocks) {body.status=FighterStatus::KO;continue;}
                body.position={0,1500,0}; body.vel_air={}; body.damage=0; body.hitstun=body.hitlag=0;
                body.grounded=false; body.status=FighterStatus::Fall; body.invincible=180;
            }
            const unsigned clip=motion(body);
            if (body.motion!=clip) {body.motion=clip;body.action_frame=0;}
            if (!body.hitlag && !services.deterministic_clock)
                for (const auto& sound:opening_motion_sounds)
                    if (sound.motion==clip && sound.frame==static_cast<unsigned>(body.action_frame)) services.audio.play_fgm(sound.fgm);
        }
        std::vector<AttackVolume> volumes;
        for (unsigned i=0;i<bodies_.size();++i) {
            const auto& body=bodies_[i];
            if (body.status!=FighterStatus::Attack || body.hitlag || body.stocks<=0) continue;
            const auto model=posed(body);
            for (const auto& box:source_jab_hitboxes)
                if (box.motion==body.motion && body.action_frame>=static_cast<int>(box.begin) && body.action_frame<static_cast<int>(box.end)) {
                    const auto position=renderer_->joint_point(model,body.action_frame,box.joint,
                        {static_cast<float>(box.x),static_cast<float>(box.y),static_cast<float>(box.z)});
                    volumes.push_back({i,position,box.radius*body.attr.size,box.damage,box.angle,box.growth,box.weight,box.base});
                }
        }
        const auto hits=FighterCombat::resolve(bodies_,volumes);
        if (!hits.empty() && !services.deterministic_clock) services.audio.play_fgm(1,.35f);
        int alive=0;
        for (unsigned i=0;i<bodies_.size();++i) if (bodies_[i].stocks>0) {++alive;winner_=static_cast<int>(i);}
        if (alive>1) winner_=-1;
    }
    void draw(Services& services) override {
        auto& r=services.render; r.begin({100,150,220,255});
        r.sprite_rect("textures/StageDreamLand.png",0,0,320,240);
        renderer_->begin();
        float left=0,right=0,top=800;
        for (const auto& body:bodies_) if (body.stocks>0) {left=std::min(left,body.position.x);right=std::max(right,body.position.x);top=std::max(top,body.position.y+body.attr.height);}
        const float center=std::clamp((left+right)*.5f,-1500.0f,1500.0f);
        const float distance=std::clamp(std::max((right-left)*1.25f,top*2),3800.0f,8000.0f);
        Camera3D camera{{center,top*.45f,distance},{center,top*.35f,0},{0,1,0},38,100,20000};
        camera.viewport={0,0,320,240};
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
    static unsigned motion(const FighterBody& body) {
        const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
        switch(body.status) {
            case FighterStatus::Walk:return data.walk;
            case FighterStatus::Dash:return data.run_clip;
            case FighterStatus::Jump:return data.jump;
            case FighterStatus::Fall:return data.fall;
            case FighterStatus::Land:return data.landing;
            case FighterStatus::Attack:return data.jab;
            case FighterStatus::Hitstun:return data.damage;
            default:return data.idle;
        }
    }
    Model3D posed(const FighterBody& body) {
        const unsigned clip=body.motion?body.motion:motion(body);
        const unsigned key=static_cast<unsigned>(body.kind)*4096+clip;
        if (!models_.contains(key)) {
            const auto spec=fighter_model_spec(body.kind);
            auto model=loader_->fighter_model(spec.descriptor,spec.joint_pairs?GeometryLayout::JointPairs:GeometryLayout::Direct,spec.setup_parts);
            model.animation=n64::AnimationDecoder(*archive_).table({clip,0},model.nodes.size());
            model.fighter_animation=true;models_.emplace(key,std::move(model));
        }
        auto model=models_.at(key);
        model.position=body.position;model.rotation.y=body.lr*std::numbers::pi_v<float>/2;
        model.scale={body.attr.size,body.attr.size,body.attr.size};return model;
    }
    int stock_,tic_{},previous_stick_y_{},winner_{-1},finish_tics_{};
    bool done_{};
    std::vector<FighterBody> bodies_;
    Stage3D stage_;
    n64::RelocArchive* archive_{};
    std::unordered_map<unsigned,Model3D> models_;
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
