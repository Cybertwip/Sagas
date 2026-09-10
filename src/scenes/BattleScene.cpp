#include <sagas/Engine.hpp>
#include <sagas/Fighter.hpp>
#include <sagas/FighterSourceData.hpp>
#include <sagas/FighterAttackData.hpp>
#include <sagas/FighterThrowData.hpp>
#include <sagas/WeaponSourceData.hpp>
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
    BattleScene(std::vector<FighterKind> fighters,int stock,std::vector<int> ports={},std::vector<std::string> models={}):ports_(std::move(ports)),stock_(stock) {
        if (ports_.empty()) {ports_.assign(fighters.size(),-1);if (!ports_.empty()) ports_[0]=0;}
        for (const auto kind:fighters) {
            FighterBody body; body.kind=kind; body.attr=fighter_attributes(kind); body.stocks=stock;
            if (bodies_.size()<models.size()) body.custom_model=models[bodies_.size()];
            bodies_.push_back(body);
        }
    }
    void enter(Services& services) override {
        assets_=&services.assets;
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
        if (finished_) {
            update_particles();
            if (++finish_tics_>=120 || input.start_pressed || input.accept_pressed || input.cancel_pressed) done_=true;
            return;
        }
        update_particles();
        for (unsigned i=0;i<bodies_.size();++i) {
            auto& body=bodies_[i];
            if (body.status==FighterStatus::KO) {advance_ko(body,services);continue;}
            const int port=ports_[i];
            const bool human=port==0 || (port>0 && input.controllers[port-1].connected);
            InputState player_input=input;
            if (port>0) {
                const auto& c=input.controllers[port-1];
                player_input.stick_x=c.x;player_input.stick_y=c.y;player_input.attack_pressed=c.attack;player_input.attack_released=c.attack_released;
                player_input.special_pressed=c.special;player_input.special_held=c.special_held;player_input.jump_pressed=c.jump;player_input.jump_released=c.jump_released;
                player_input.shield_held=c.shield;player_input.shield_pressed=c.shield_pressed;player_input.grab_pressed=c.grab;player_input.taunt_pressed=c.taunt;
            }
            if (body.stocks<=0) continue;
            const int old_x=body.stick_x,old_y=body.stick_y;
            bool attack=false;
            if (human) {
                body.stick_x=static_cast<int>(player_input.stick_x); body.stick_y=static_cast<int>(player_input.stick_y);
                body.jump_pressed=player_input.jump_pressed || (player_input.tap_jump && body.stick_y>=53 && old_y<53);
                if (body.jump_pressed) body.jump_button=player_input.jump_pressed;
                body.jump_released=player_input.jump_released;
                body.shield_held=player_input.shield_held; attack=player_input.attack_pressed;
                body.shield_tics=player_input.shield_pressed?0:std::min(255,body.shield_tics+1);

            } else {
                // Deterministic CPU approach; the same body/attack/collision
                // path is used for humans and CPU fighters.
                const auto target=std::find_if(bodies_.begin(),bodies_.end(),[&](const auto& other){return &other!=&body && other.stocks>0;});
                const bool target_offstage=target!=bodies_.end() && (target->status==FighterStatus::CliffCatch || target->status==FighterStatus::CliffWait || target->position.y<0);
                const float target_x=target_offstage?std::clamp(target->position.x,-900.f,900.f):target!=bodies_.end()?target->position.x:0;
                const float dx=target_x-body.position.x;
                const float dy=target!=bodies_.end()?target->position.y-body.position.y:0;
                body.stick_x=std::abs(dx)>260?(dx>0?60:-60):0; body.stick_y=0;
                if (std::abs(dx)>1 && body.status!=FighterStatus::Attack && body.status!=FighterStatus::Hitstun && body.status!=FighterStatus::Tumble && !fighter_is_down(body.status)) body.lr=dx>0?1:-1;
                body.jump_pressed=(body.grounded && dy>300 && !target_offstage && tic_%40==0) || (!body.grounded && body.position.y<0 && body.jumps_used<body.attr.jumps_max && body.vel_air.y<0);
                body.jump_button=true; body.jump_released=false;
                attack=std::abs(dx)<420 && tic_%32==static_cast<int>(i)*3;
            }
            body.tap_stick_x=std::abs(body.stick_x)>=56 && (std::abs(old_x)<56 || old_x*body.stick_x<0)?0:std::min(255,body.tap_stick_x+1);
            body.tap_stick_y=std::abs(body.stick_y)>=53 && (std::abs(old_y)<53 || old_y*body.stick_y<0)?0:std::min(255,body.tap_stick_y+1);
            FighterCombat::buffer_smash(body,attack);
            FighterCombat::buffer_aerial(body,attack);
            body.special_held=human && player_input.special_held;
            const bool on_cliff=body.status==FighterStatus::CliffCatch || body.status==FighterStatus::CliffWait || body.status==FighterStatus::CliffClimb;
            if (on_cliff) update_cliff(body);
            if (!on_cliff && !body.hitlag && body.status!=FighterStatus::Captured) {
                if (body.status!=FighterStatus::DownWait) ++body.action_frame;
                if (fighter_is_down(body.status))
                    FighterCombat::advance_down(body,attack,human && player_input.shield_pressed,body.action_frame>=motion_length(body));
                if (body.status==FighterStatus::Hitstun) {
                    body.hitstun=std::max(0,body.hitstun-1);
                    if (!body.hitstun && (body.action_frame>=motion_length(body) || attack || body.jump_pressed || (body.grounded && (std::abs(body.stick_x)>=8 || body.stick_y<=-53 || body.shield_held)))) {
                        body.status=body.grounded?FighterStatus::Wait:body.damage_tumble?FighterStatus::Tumble:FighterStatus::Fall;
                        body.action_frame=0;
                    }
                }
                if (body.status==FighterStatus::Turn && body.action_frame>=motion_length(body)) body.status=FighterStatus::Wait;
                if (body.status==FighterStatus::Crouch && body.action_frame>=motion_length(body)) {
                    body.status=FighterStatus::CrouchWait;body.action_frame=0;
                } else if (body.status==FighterStatus::CrouchEnd && body.action_frame>=motion_length(body)) {
                    body.status=FighterStatus::Wait;body.action_frame=0;
                }
                if (body.status==FighterStatus::Special)
                    FighterCombat::advance_special(body,human && player_input.special_pressed,body.action_frame>=motion_length(body));
                (void)FighterCombat::start_special(body,human && player_input.special_pressed);
                if (human && player_input.taunt_pressed && body.grounded && (body.status==FighterStatus::Wait || body.status==FighterStatus::Walk)) {
                    body.status=FighterStatus::Attack;body.attack_motion=fighter_source_data[static_cast<unsigned>(body.kind)].taunt;body.action_frame=0;
                }
                const bool grab=FighterCombat::start_grab(body,(human && player_input.grab_pressed) || (attack && body.shield_held));
                const bool aerial=FighterCombat::start_aerial(body,attack && !grab);
                const bool dash_attack=FighterCombat::start_dash_attack(body,attack && body.aerial_buffer<=0 && !grab && !aerial && body.smash_buffer<=0);
                const bool smash=FighterCombat::start_smash(body,attack && body.aerial_buffer<=0 && !grab && !aerial && !dash_attack);
                if (body.status==FighterStatus::Land && body.landing_motion && body.action_frame*body.landing_speed>=motion_length(body)) {
                    body.landing_motion=0;body.status=FighterStatus::Wait;body.action_frame=0;
                }
                if (body.status==FighterStatus::Catch && body.action_frame>=motion_length(body)) body.status=FighterStatus::Wait;
                if (body.status==FighterStatus::Throw && body.capture_target<0 && body.action_frame>=motion_length(body)) body.status=FighterStatus::Wait;
                if (body.status==FighterStatus::CatchWait && body.action_frame>=motion_length(body) &&
                    (++body.capture_tics>=60 || attack || (std::abs(body.stick_x)>=20 && (std::abs(old_x)<20 || old_x*body.stick_x<0)))) {
                    body.throw_backward=!attack && body.capture_tics<60 && body.stick_x*body.lr<0;
                    body.status=FighterStatus::Throw;body.action_frame=0;
                }
                const bool tilt=FighterCombat::start_tilt(body,attack && body.aerial_buffer<=0 && !grab && !aerial && !smash);
                const bool ended=(body.status==FighterStatus::Attack || body.status==FighterStatus::Jump || body.status==FighterStatus::Dash) &&
                                  body.action_frame>=motion_length(body);
                FighterCombat::advance_jab(body,attack && !body.jump_pressed && body.aerial_buffer<=0 && !smash && !aerial && !grab && !tilt && !dash_attack,ended,human && player_input.attack_released);
                if (body.status==FighterStatus::Jump && ended) body.status=FighterStatus::Fall;
                if (body.status==FighterStatus::Dash && ended) {body.status=FighterStatus::Wait;body.vel_ground*=.75f;}
                const auto previous_guard=body.status;
                FighterCombat::advance_guard(body,body.action_frame>=motion_length(body));
                if (!services.deterministic_clock && previous_guard!=body.status) {
                    if (body.status==FighterStatus::Shield) services.audio.play_fgm(guard_on_sfx);
                    if (body.status==FighterStatus::ShieldRelease) services.audio.play_fgm(guard_off_sfx);
                }
                if (body.status==FighterStatus::Shield) body.shield=std::max(0.f,body.shield-.15f);
                if (body.status!=FighterStatus::Shield) body.shield=std::min(55.0f,body.shield+.05f);
            }
            // Source DownBounce chooses the side from joint 4's current X rotation.
            if (body.status==FighterStatus::Hitstun || body.status==FighterStatus::Tumble) {
                const auto actor=posed(body,true);
                const auto found=std::find(actor.source_joint_ids.begin(),actor.source_joint_ids.end(),4U);
                if (found!=actor.source_joint_ids.end()) {
                    const auto joint=static_cast<unsigned>(found-actor.source_joint_ids.begin());
                    n64::AnimationDecoder decoder(*archive_);
                    auto pose=decoder.pose(actor.nodes[joint]);
                    if (actor.animation[joint]) pose=decoder.sample16(*actor.animation[joint],body.action_frame,pose);
                    const float turns=std::fmod(pose.tracks[0]/(2*std::numbers::pi_v<float>),1.f);
                    body.down_face=(turns<-.5f || (turns>0 && turns<.5f))?0:1;
                }
            }
            const bool was_grounded=body.grounded;
            const auto before=body.position;
            if (!on_cliff) FighterPhysics::tick(body,stage_.collision,[&](const FighterBody& jumping) -> std::optional<Vec3> {
                const auto model=posed(jumping,true);
                if (!model.fighter_root_animation || (jumping.grounded && model.fighter_wrapper!=Model3D::FighterWrapper::TransN)) return {};
                n64::AnimationDecoder decoder(*archive_);
                float ended=-1;
                const int frame=(jumping.grounded || jumping.status==FighterStatus::Special)?std::max(0,jumping.action_frame-1):jumping.jump_frames;
                const auto previous=decoder.sample16(*model.fighter_root_animation,frame,
                                                     decoder.pose(model.fighter_root),&ended);
                if (ended>=0) return {};
                const auto current=decoder.sample16(*model.fighter_root_animation,(jumping.grounded || jumping.status==FighterStatus::Special)?jumping.action_frame:frame+1,
                                                    decoder.pose(model.fighter_root));
                const float z=(current.tracks[6]-previous.tracks[6])*jumping.lr*jumping.attr.size;
                const float y=(current.tracks[5]-previous.tracks[5])*jumping.attr.size;
                if (jumping.grounded) return Vec3{z,jumping.status==FighterStatus::Special?y:0,-(current.tracks[4]-previous.tracks[4])*jumping.lr*jumping.attr.size};
                const float angle=current.tracks[2];
                return Vec3{z*std::cos(angle)-y*std::sin(angle),z*std::sin(angle)+y*std::cos(angle),0};
            });
            if (!on_cliff && FighterPhysics::try_ledge(body,before,stage_.collision,bodies_)) update_cliff(body,false);
            if (!was_grounded && body.grounded) {
                emit(body.position,{215,225,235,220},body.status==FighterStatus::DownBounce?16:7,false);
                if (!services.deterministic_clock) {
                    const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
                    services.audio.play_fgm(body.status==FighterStatus::DownBounce?data.down_sfx:data.landing_sfx);
                }
            }
            if (body.grounded && (body.status==FighterStatus::Dash || body.status==FighterStatus::RunBrake) && tic_%4==0)
                emit(body.position,{225,225,220,170},3,false);
            const auto& bounds=stage_.blast_bounds;
            if (body.position.x<bounds[3] || body.position.x>bounds[2] || body.position.y<bounds[1] || body.position.y>bounds[0]) {
                begin_ko(body,services);continue;
            }
            const unsigned clip=motion(body);
            if (body.motion!=clip) {
                const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
                const auto old=std::find(data.walks.begin(),data.walks.end(),body.motion);
                const auto next=std::find(data.walks.begin(),data.walks.end(),clip);
                if (body.status==FighterStatus::Walk && old!=data.walks.end() && next!=data.walks.end())
                    body.action_frame=static_cast<int>(body.action_frame*data.walk_lengths[next-data.walks.begin()]/data.walk_lengths[old-data.walks.begin()]);
                else if (body.status!=FighterStatus::Special) body.action_frame=0;
                body.motion=clip;
            }
            const unsigned event_clip=body.status==FighterStatus::Special?FighterCombat::special_event_motion(body):clip;
            if (body.status==FighterStatus::Special && body.kind==FighterKind::Fox && body.special_index%3==1 && tic_%3==0)
                emit({body.position.x,body.position.y+body.attr.height*.5f,0},{255,160,60,220},3,true);
            if (body.status==FighterStatus::Special && !body.special_projectile) {
                for (const auto& flag:source_special_flags)
                    if (flag.kind==static_cast<unsigned>(body.kind) && flag.motion==event_clip && flag.flag==0 && flag.value && flag.frame<=static_cast<unsigned>(body.action_frame)) {
                        spawn_projectile(i,body);body.special_projectile=true;break;
                    }
            }
            body.recovery_invulnerable=false;
            if (fighter_is_down(body.status) || body.status==FighterStatus::ShieldRoll || body.status==FighterStatus::Special) {
                unsigned hit_status=1;
                for (const auto& event:source_hit_status)
                    if (event.kind==static_cast<unsigned>(body.kind) && event.motion==event_clip && event.frame<=static_cast<unsigned>(body.action_frame)) hit_status=event.status;
                body.recovery_invulnerable=hit_status!=1;
            }
            if (!body.hitlag && (body.audio_motion!=event_clip || body.audio_frame!=body.action_frame)) {
                body.audio_motion=event_clip;body.audio_frame=body.action_frame;
                if (!services.deterministic_clock) for (const auto& sound:battle_motion_sounds)
                    if (sound.motion==event_clip && sound.frame==static_cast<unsigned>(body.action_frame)) {
                        const auto& voices=fighter_source_data[static_cast<unsigned>(body.kind)].smash_voices;
                        unsigned fgm=sound.fgm==~0U?voices[(tic_+i)%3]:sound.fgm;
                        if (body.kind==FighterKind::Luigi)
                            for (unsigned v=0;v<3;++v)
                                if (fgm==fighter_source_data[static_cast<unsigned>(FighterKind::Mario)].smash_voices[v]) {fgm=voices[v];break;}
                        services.audio.play_character_fgm(body.custom_model,fgm);
                    }
            }
        }
        // Resolve capture links after both fighters have advanced, so player order
        // cannot move the captive twice or leave a stale link after interruption.
        for (unsigned i=0;i<bodies_.size();++i) {
            auto& holder=bodies_[i];
            if (holder.capture_target<0) continue;
            auto& captive=bodies_[holder.capture_target];
            const bool dive=holder.kind==FighterKind::Captain && holder.status==FighterStatus::Special && holder.special_phase==4;
            const bool release=holder.stocks<=0 || (!holder.grounded && !dive) ||
                (!dive && holder.status!=FighterStatus::CatchWait && holder.status!=FighterStatus::Throw) ||
                (holder.status==FighterStatus::CatchWait && holder.capture_tics>180);
            if (release) {
                captive.captured_by=-1;captive.status=FighterStatus::Fall;captive.grounded=false;
                holder.capture_target=-1;
                if (holder.status==FighterStatus::CatchWait) holder.status=FighterStatus::Wait;
                continue;
            }
            captive.lr=-holder.lr;captive.captured_throw=holder.status==FighterStatus::Throw;
            captive.action_frame=dive?4:holder.action_frame;
            const auto holder_model=posed(holder);
            const unsigned joint=dive?29:fighter_source_data[static_cast<unsigned>(holder.kind)].capture_joint;
            const auto anchor=renderer_->joint_point(holder_model,holder.action_frame,joint);
            const auto up=renderer_->joint_point(holder_model,holder.action_frame,joint,{0,1,0});
            captive.capture_rotation=std::atan2(up.y-anchor.y,up.x-anchor.x)-std::numbers::pi_v<float>/2;
            auto captive_model=posed(captive);captive_model.position={};
            const auto root=renderer_->joint_point(captive_model,captive.action_frame,4);
            captive.position={anchor.x-root.x,anchor.y-root.y,anchor.z-root.z};
            const auto& damage=source_throws[static_cast<unsigned>(holder.kind)][holder.throw_backward?1:0];
            bool dive_release=false;
            if (dive) for (const auto& flag:source_special_flags)
                if (flag.kind==static_cast<unsigned>(holder.kind) && flag.motion==FighterCombat::special_event_motion(holder) && flag.flag==0 && flag.value && flag.frame<=static_cast<unsigned>(holder.action_frame)) dive_release=true;
            if (dive_release || (holder.status==FighterStatus::Throw && holder.action_frame>=damage.frame)) {
                captive.captured_by=-1;captive.status=FighterStatus::Wait;captive.invincible=0;
                // Reuse the damage/knockback path with only the captured target eligible.
                const unsigned saved=holder.hit_mask;
                holder.hit_mask=~(1U<<holder.capture_target);
                const int facing=holder.lr;
                if (holder.throw_backward) holder.lr=-holder.lr;
                AttackVolume hit{i,{captive.position.x,captive.position.y+captive.attr.width,0},1,
                    dive?20:damage.damage,dive?361:damage.angle,dive?82:damage.growth,dive?0:damage.weight,dive?30:damage.base,0};
                (void)FighterCombat::resolve(bodies_,std::span<const AttackVolume>(&hit,1));
                holder.lr=facing;holder.hit_mask=saved;holder.capture_target=-1;
                if (dive) {holder.special_phase=2;holder.special_motion=fighter_source_data[static_cast<unsigned>(holder.kind)].special_end[holder.special_index];holder.action_frame=0;emit(captive.position,{255,180,60,255},20,true);}
            }
        }
        for (unsigned i=0;i<bodies_.size();++i) {
            auto& captive=bodies_[i];
            if (captive.captured_by>=0 && (bodies_[captive.captured_by].capture_target!=static_cast<int>(i) || bodies_[captive.captured_by].stocks<=0)) {
                captive.captured_by=-1;
                if (captive.status==FighterStatus::Captured) {captive.status=FighterStatus::Fall;captive.grounded=false;}
            }
        }
        std::vector<AttackVolume> volumes;
        for (unsigned i=0;i<bodies_.size();++i) {
            auto& body=bodies_[i];
            if ((body.status!=FighterStatus::Attack && body.status!=FighterStatus::Special && body.status!=FighterStatus::Catch && body.status!=FighterStatus::DownAttack) || body.hitlag || body.stocks<=0) continue;
            const auto model=posed(body);
            for (const auto& box:source_jab_hitboxes)
                if (box.kind==static_cast<unsigned>(body.kind) && box.motion==(body.status==FighterStatus::Special?FighterCombat::special_event_motion(body):body.motion) && body.action_frame>=static_cast<int>(box.begin) && body.action_frame<static_cast<int>(box.end)) {
                    const auto position=renderer_->joint_point(model,body.action_frame,box.joint,
                        {static_cast<float>(box.x),static_cast<float>(box.y),static_cast<float>(box.z)});
                    volumes.push_back({i,position,box.radius*.5f*body.attr.size,box.damage,box.angle,box.growth,box.weight,box.base,box.fgm,(body.status==FighterStatus::Catch || (body.kind==FighterKind::Captain && body.special_index%3==1 && body.status==FighterStatus::Special && body.special_phase==0)),box.group,box.epoch,box.element});
                }
        }
        auto hits=FighterCombat::resolve(bodies_,volumes);
        for (auto& shot:projectiles_) {
            if (--shot.life<=0) continue;
            const auto before=shot.position;
            shot.position.x+=shot.velocity.x;shot.position.y+=shot.velocity.y;shot.velocity.y-=shot.gravity;
            for (const auto& floor:stage_.collision) if (floor.type==0 && floor.a.x!=floor.b.x && shot.velocity.y<0 && shot.position.x>=std::min(floor.a.x,floor.b.x) && shot.position.x<=std::max(floor.a.x,floor.b.x)) {
                const float y=floor.a.y+(floor.b.y-floor.a.y)*(shot.position.x-floor.a.x)/(floor.b.x-floor.a.x);
                if (before.y>=y && shot.position.y<y) {
                    shot.position.y=y+10;
                    if (shot.weapon==0 || shot.weapon==1) shot.velocity.y=std::abs(shot.velocity.y)*.85f;
                    else if (shot.weapon==6) {shot.weapon=8;shot.velocity={55.f*shot.facing,0,0};shot.gravity=0;}
                    else shot.life=0;
                }
            }
            if (shot.weapon==7) {
                auto& owner=bodies_[shot.owner];
                if (owner.status!=FighterStatus::Special || owner.special_index%3!=2) shot.life=0;
                else if (std::abs(owner.position.x-shot.position.x)<200 && std::abs(owner.position.y-shot.position.y-225)<800) {
                    owner.special_phase=4;owner.special_motion=fighter_source_data[static_cast<unsigned>(owner.kind)].special_hit[owner.special_index];owner.action_frame=0;
                    if (!owner.grounded) owner.vel_air.y=20;
                    shot.life=0;emit(owner.position,{130,200,255,255},24,true);
                }
                // Thunder leaves damaging segments behind the descending head.
                for (int segment=0;segment<5;++segment)
                    particles_.push_back({{shot.position.x,shot.position.y+segment*90.f,0},{},{160,215,255,255},0,10,110,true});
            }
            if (shot.weapon==8) {
                bool supported=false;
                for (const auto& floor:stage_.collision) if (floor.type==0 && floor.a.x!=floor.b.x && shot.position.x>=std::min(floor.a.x,floor.b.x) && shot.position.x<=std::max(floor.a.x,floor.b.x)) {
                    const float y=floor.a.y+(floor.b.y-floor.a.y)*(shot.position.x-floor.a.x)/(floor.b.x-floor.a.x);
                    if (std::abs(y-shot.position.y)<120) {shot.position.y=y+10;supported=true;break;}
                }
                if (!supported) {shot.weapon=6;shot.velocity={28.28427f*shot.facing,-28.28427f,0};}
            }
            if (shot.weapon==4 && shot.life<80) shot.velocity.x+=(bodies_[shot.owner].position.x>shot.position.x?3.f:-3.f);
            for (unsigned target=0;target<bodies_.size();++target) {
                const auto& reflector=bodies_[target];
                if (target==shot.owner || reflector.kind!=FighterKind::Fox || reflector.status!=FighterStatus::Special || reflector.special_index%3!=2) continue;
                if (std::hypot(shot.position.x-reflector.position.x,shot.position.y-reflector.position.y-reflector.attr.height*.5f)<reflector.attr.height*.6f) {
                    shot.owner=target;shot.velocity.x=-shot.velocity.x;shot.velocity.y=-shot.velocity.y;shot.facing=-shot.facing;
                    emit(shot.position,{120,210,255,255},10,true);break;
                }
            }
            if (shot.life<=0) continue;
            const auto& a=weapon_source_data[shot.weapon];
            AttackVolume contact{shot.owner,shot.position,a.size*.5f,a.damage,a.angle,a.growth,a.weight,a.base,static_cast<unsigned>(a.sfx),false,0,~0U,static_cast<unsigned>(a.element),true,shot.facing,shot.hit_mask};
            auto contacts=FighterCombat::resolve(bodies_,std::span<const AttackVolume>(&contact,1));
            for (const auto& contact_hit:contacts) shot.hit_mask|=1U<<contact_hit.defender;
            if (!contacts.empty()) {if (shot.weapon!=7) shot.life=0;hits.insert(hits.end(),contacts.begin(),contacts.end());}
            const Color color=a.element==2?Color{130,200,255,255}:shot.weapon==2?Color{255,80,80,255}:Color{255,160,55,255};
            particles_.push_back({shot.position,{},color,0,3,shot.weapon==2?50.f:90.f,true});
        }
        std::erase_if(projectiles_,[](const auto& shot){return shot.life<=0;});
        for (const auto& hit:hits) {
            const auto& victim=bodies_[hit.defender];
            emit({victim.position.x,victim.position.y+victim.attr.height*.5f,0},
                 hit.shield?Color{100,175,255,255}:hit.element==2?Color{125,195,255,255}:Color{255,235,130,255},hit.shield?6:12,true);
            if (!services.deterministic_clock) services.audio.play_fgm(hit.fgm);
        }
        int alive=0;
        for (unsigned i=0;i<bodies_.size();++i) if (bodies_[i].stocks>0) {++alive;winner_=static_cast<int>(i);}
        if (alive>1) winner_=-1;
        else {finished_=true;finish_tics_=0;}
        camera_.tick(bodies_,stage_);
    }
    void draw(Services& services) override {
        auto& r=services.render; r.begin({100,150,220,255});
        r.sprite_rect("textures/StageDreamLand.png",0,0,320,240);
        renderer_->begin();
        const auto& camera=camera_.view();
        for (const auto& layer:stage_.layers) renderer_->draw(r,layer,camera,static_cast<float>(tic_));
        for (const auto& body:bodies_) {
            if (body.stocks<=0 || (body.status==FighterStatus::KO && (body.ko_mode==3 || body.ko_tics>=180)) || (body.invincible && tic_%6<2)) continue;
            const auto model=posed(body);
            renderer_->draw(r,model,camera,body.action_frame*(body.status==FighterStatus::Land?body.landing_speed:1.f),
                            body.status==FighterStatus::Shield?Color{130,160,255,255}:Color{255,255,255,255});
        }
        for (const auto& shot:projectiles_) if (shot.weapon==6 || shot.weapon==8 || shot.weapon==7) {
            if (!weapon_models_.contains(shot.weapon)) {
                const n64::Address attributes{shot.weapon==7?243U:244U,shot.weapon==7?64U:shot.weapon==8?52U:0U};
                weapon_models_.emplace(shot.weapon,loader_->weapon(attributes,shot.weapon==6?0:shot.weapon==8?3:2));
            }
            if (weapon_models_.contains(shot.weapon)) {
                auto weapon=weapon_models_.at(shot.weapon);weapon.position=shot.position;weapon.rotation.y=shot.facing*std::numbers::pi_v<float>/2;
                if (shot.weapon==7) weapon.scale={.5f,.5f,.5f};
                renderer_->draw(r,weapon,camera,static_cast<float>(tic_));
            }
        }
        renderer_->end(r);
        draw_particles(r,camera);
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
        if (finished_) {
            r.fill(0,88,320,52,{0,0,0,175});
            const std::string label="GAME SET";
            float x=71;
            for (char c:label) {
                if (c!=' ') r.sprite_rect("textures/IFCommonAnnounceCommon/Letter"+std::string(1,c)+".png",x,97,19,24,{255,235,100,255});
                x+=23;
            }
            if (winner_>=0) r.sprite_rect("textures/MNPlayersPortraits/"+std::string(fighter_portrait_file(bodies_[winner_].kind)),144,146,32,32);
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
    void begin_ko(FighterBody& body,Services& services) {
        const bool upward=body.position.y>stage_.blast_bounds[0];
        body.ko_mode=upward?((ko_serial_++%6)==5?2:1):3;body.ko_tics=0;
        body.status=FighterStatus::KO;body.action_frame=0;body.hitlag=0;body.hitstun=0;body.capture_target=-1;body.captured_by=-1;
        body.vel_air={};body.vel_damage={};body.vel_ground=0;body.grounded=false;
        const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
        if (upward) {
            if (!services.deterministic_clock && data.deadup_sfx!=~0U) services.audio.play_character_fgm(body.custom_model,data.deadup_sfx);
            if (body.ko_mode==1) body.vel_air={0,(stage_.camera_bounds[0]*.6f-body.position.y)/180.f,-83.333336f};
            else {
                const auto& eye=camera_.view().eye;
                body.position={eye.x,std::min(eye.y+3000,stage_.blast_bounds[0]),std::max(2000.f,eye.z-3000)};
                body.vel_air.y=(stage_.camera_bounds[1]-body.position.y)/180.f;
            }
        } else {
            --body.stocks;
            emit({std::clamp(body.position.x,stage_.camera_bounds[3],stage_.camera_bounds[2]),std::clamp(body.position.y,stage_.camera_bounds[1],stage_.camera_bounds[0]),0},{255,230,120,255},32,true);
            if (!services.deterministic_clock) {services.audio.play_fgm(dead_explode_sfx);for(auto sound:data.dead_sfx) if(sound!=~0U) services.audio.play_character_fgm(body.custom_model,sound);}
        }
    }
    void advance_ko(FighterBody& body,Services& services) {
        if (body.ko_mode==0) return;
        ++body.ko_tics;++body.action_frame;
        if (body.ko_mode!=3 && body.ko_tics<=180) {
            body.position.x+=body.vel_air.x;body.position.y+=body.vel_air.y;body.position.z+=body.vel_air.z;
            if (body.ko_tics==180) {
                --body.stocks;
                emit(body.position,{255,255,255,255},20,true);
                if (body.ko_mode==1) particles_.push_back({body.position,{},{255,255,255,255},0,30,700,true,false,10});
                if (!services.deterministic_clock) services.audio.play_fgm(body.ko_mode==1?dead_star_sfx:dead_explode_sfx);
            }
        }
        if (body.ko_tics>=(body.ko_mode==3?45:225) && body.stocks>0) {
            const auto kind=body.kind;const auto attr=body.attr;const int stocks=body.stocks;const auto package=body.custom_model;
            body={};body.custom_model=package;body.kind=kind;body.attr=attr;body.stocks=stocks;
            body.position={0,1500,0};body.grounded=false;body.status=FighterStatus::Fall;body.invincible=120;
        }
    }
    unsigned ko_serial_{};
    static unsigned motion(const FighterBody& body) {
        const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
        switch(body.status) {
            case FighterStatus::KO:return data.damage_reactions[19];
            case FighterStatus::Shield:case FighterStatus::ShieldRelease:case FighterStatus::ShieldRoll:return body.guard_motion;
            case FighterStatus::Crouch:return data.crouch[0];
            case FighterStatus::CrouchWait:return data.crouch[1];
            case FighterStatus::CrouchEnd:return data.crouch[2];
            case FighterStatus::Turn:return data.turn;
            case FighterStatus::Walk:return data.walks[std::abs(body.stick_x)>=62?2:std::abs(body.stick_x)>=26?1:0];
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
            case FighterStatus::SpecialFall:case FighterStatus::Fall:return data.fall;
            case FighterStatus::Land:return body.landing_motion?body.landing_motion:data.landing;
            case FighterStatus::Catch:return data.grab[0];
            case FighterStatus::CatchWait:return data.grab[1];
            case FighterStatus::Captured:return data.capture[body.captured_dive?2:body.captured_throw?1:0];
            case FighterStatus::Throw:return data.grab[body.throw_backward?3:2];
            case FighterStatus::Special:return body.special_motion;
            case FighterStatus::Attack:
                if (body.attack_motion) return body.attack_motion;
                if (body.jab_stage>=4) return data.rapid[body.jab_stage-4];
                return body.jab_stage==3?data.jab3:body.jab_stage==2?data.jab2:data.jab;
            case FighterStatus::Hitstun:return body.damage_motion?body.damage_motion:data.damage_reactions[3];
            case FighterStatus::Tumble:return data.damage_reactions[19];
            case FighterStatus::DownBounce:case FighterStatus::DownWait:case FighterStatus::DownStand:
            case FighterStatus::DownRoll:case FighterStatus::DownAttack:return body.down_motion;
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
            const unsigned flags=fighter_motion_flags(clip);
            models_.emplace(key,loader_->fighter_motion(body.kind,clip,flags));
        }
        auto model=models_.at(key);
        if (!body.custom_model.empty()) {
            const auto custom_key=body.custom_model+":"+std::to_string(key);
            if (!custom_models_.contains(custom_key)) {
                auto imported=model;const auto bytes=assets_->blob(body.custom_model);
                loader_->apply_custom_mesh(imported,*bytes);custom_models_.emplace(custom_key,std::move(imported));
            }
            model=custom_models_.at(custom_key);
        }
        if (!with_root && model.fighter_wrapper==Model3D::FighterWrapper::TransN) model.fighter_root_animation.reset();
        model.position=body.position;model.rotation.y=body.lr*std::numbers::pi_v<float>/2;
        if (body.status==FighterStatus::Captured) model.rotation.z=body.capture_rotation;
        if (body.status==FighterStatus::Special && body.kind==FighterKind::Fox && body.special_phase==3)
            model.rotation.z=body.lr*std::numbers::pi_v<float>/2-std::atan2(body.special_velocity.x,body.special_velocity.y);
        model.scale={body.attr.size,body.attr.size,body.attr.size};
        if (body.kind==FighterKind::Pikachu && body.status==FighterStatus::Special && body.special_index%3==1 && body.special_phase==3) {
            model.rotation.z=body.lr*std::numbers::pi_v<float>/2-std::atan2(body.special_velocity.x,body.special_velocity.y);
            model.scale={body.attr.size*.8f,body.attr.size*.8f,body.attr.size*1.2f};
        }
        return model;
    }
    std::unordered_map<unsigned,Model3D> weapon_models_;
    struct Projectile { unsigned owner,weapon;Vec3 position,velocity;int life,facing;float gravity;unsigned hit_mask{}; };
    std::vector<Projectile> projectiles_;
    void spawn_projectile(unsigned owner,const FighterBody& body) {
        if (body.kind==FighterKind::Pikachu && body.special_index%3==2) {
            const auto anchor=renderer_->joint_point(posed(body),body.action_frame,11);
            projectiles_.push_back({owner,7,{anchor.x,stage_.blast_bounds[0]-500,0},{0,-450,0},40,body.lr,0});return;
        }
        if (body.special_index%3!=0) return;
        unsigned weapon;
        switch (body.kind) {
            case FighterKind::Luigi:weapon=0;break;case FighterKind::Mario:weapon=1;break;
            case FighterKind::Fox:weapon=2;break;case FighterKind::Samus:weapon=3;break;
            case FighterKind::Link:weapon=4;break;case FighterKind::Ness:weapon=5;break;
            case FighterKind::Pikachu:weapon=6;break;default:return;
        }
        const float speed=weapon==2?160.f:weapon==4?85.f:weapon==6?28.28427f:50.f;
        Vec3 origin{body.position.x+body.lr*(body.attr.width+60),body.position.y+body.attr.height*.6f,0};
        if (weapon==6) origin=renderer_->joint_point(posed(body),body.action_frame,11);
        projectiles_.push_back({owner,weapon,origin,{body.lr*speed,weapon==1?-4.3578f:weapon==6?-28.28427f:0,0},weapon==0?80:weapon==1?140:weapon==6?100:160,body.lr,weapon==1?1.2f:0.f});
    }
    struct Particle { Vec3 position,velocity; Color color; int age{},life{}; float size{}; bool spark{},ring{}; int sides{16}; };
    std::vector<Particle> particles_;
    void emit(Vec3 origin,Color color,int count,bool spark) {
        for (int i=0;i<count;++i) {
            const float angle=(i*2.39996323f+tic_*.7f);
            const float speed=spark?18.f+(i%4)*10.f:6.f+(i%3)*4.f;
            particles_.push_back({origin,{std::cos(angle)*speed,std::abs(std::sin(angle))*speed,0},color,0,spark?14:22,spark?34.f:50.f,spark});
        }
        if (particles_.size()>256) particles_.erase(particles_.begin(),particles_.end()-256);
    }
    void update_particles() {
        for (auto& p:particles_) {++p.age;p.position.x+=p.velocity.x;p.position.y+=p.velocity.y;p.velocity.y-=p.spark?2.f:.3f;}
        std::erase_if(particles_,[](const auto& p){return p.age>=p.life;});
    }
    void draw_particles(RenderEngine& r,const Camera3D& camera) {
        const auto normalize=[](Vec3 v){float n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);return Vec3{v.x/n,v.y/n,v.z/n};};
        const auto cross=[](Vec3 a,Vec3 b){return Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
        const auto dot=[](Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
        const auto forward=normalize(Vec3{camera.at.x-camera.eye.x,camera.at.y-camera.eye.y,camera.at.z-camera.eye.z});
        const auto right=normalize(cross(forward,camera.up)),up=cross(right,forward);
        auto displayed=particles_;
        for (const auto& body:bodies_) if (body.status==FighterStatus::Shield || (body.status==FighterStatus::Special && body.kind==FighterKind::Fox && body.special_index%3==2)) {
            const bool shield=body.status==FighterStatus::Shield;
            const float radius=body.attr.height*(shield?.42f+.25f*body.shield/55.f:.6f);
            displayed.push_back({{body.position.x,body.position.y+body.attr.height*.5f,body.position.z},{},shield?Color{255,100,120,180}:Color{100,210,255,210},0,1,radius,false,true,shield?32:6});
        }
        for (const auto& p:displayed) {
            const Vec3 delta{p.position.x-camera.eye.x,p.position.y-camera.eye.y,p.position.z-camera.eye.z};
            const float depth=dot(delta,forward);if (depth<=camera.near_plane) continue;
            const float factor=120/(depth*std::tan(camera.fov_y*std::numbers::pi_v<float>/360));
            const float x=160+dot(delta,right)*factor*.75f,y=120-dot(delta,up)*factor;
            const float size=p.size*factor*(p.spark?1.f:1.f+p.age*.05f);
            auto color=p.color;color.a=static_cast<std::uint8_t>(color.a*(1.f-float(p.age)/p.life));
            std::vector<TriangleVertex> shape;shape.reserve(48);
            Color edge=color;edge.a=p.sides==10?color.a:p.ring?220:0;
            if (p.ring) color.a=35;
            for (int segment=0;segment<p.sides;++segment) {
                const float a=segment*2*std::numbers::pi_v<float>/p.sides,b=(segment+1)*2*std::numbers::pi_v<float>/p.sides;
                shape.push_back({{x,y},color,{}});
                const float ra=p.sides==10 && segment%2?.35f:1.f,rb=p.sides==10 && (segment+1)%2?.35f:1.f;
                shape.push_back({{x+std::cos(a)*size*.75f*ra,y+std::sin(a)*size*ra},edge,{}});
                shape.push_back({{x+std::cos(b)*size*.75f*rb,y+std::sin(b)*size*rb},edge,{}});
            }
            r.triangles(shape);
            if (p.spark && p.color.b>p.color.r && p.color.b>200) {
                std::vector<TriangleVertex> bolts;
                for (int branch=0;branch<4;++branch) {
                    const float a=branch*1.57079633f+p.age*.4f;
                    Vec2 previous{x,y};
                    for (int step=1;step<=4;++step) {
                        const float twist=a+((step%2)?-.45f:.3f),distance=size*step*.6f;
                        const Vec2 next{x+std::cos(twist)*distance*.75f,y+std::sin(twist)*distance};
                        const Color flash{225,250,255,color.a};const float width=.45f;
                        bolts.insert(bolts.end(),{{{previous.x-width,previous.y},flash,{}},{{previous.x+width,previous.y},flash,{}},{{next.x+width,next.y},flash,{}},
                            {{previous.x-width,previous.y},flash,{}},{{next.x+width,next.y},flash,{}},{{next.x-width,next.y},flash,{}}});
                        previous=next;
                    }
                }
                r.triangles(bolts);
            }
        }
    }
    AssetRepository* assets_{};
    std::unordered_map<std::string,Model3D> custom_models_;
    std::vector<int> ports_;
    int stock_,tic_{},winner_{-1},finish_tics_{};
    bool done_{},finished_{};
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
std::unique_ptr<Scene> make_battle_scene(std::vector<FighterKind> fighters,int stock,std::vector<int> ports,std::vector<std::string> models) {
    return std::make_unique<BattleScene>(std::move(fighters),stock,std::move(ports),std::move(models));
}
std::unique_ptr<Scene> make_battle_scene(FighterKind p1,FighterKind p2,int stock) {
    return make_battle_scene(std::vector<FighterKind>{p1,p2},stock);
}
}
