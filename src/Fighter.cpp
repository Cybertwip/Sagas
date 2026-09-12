#include <sagas/Fighter.hpp>
#include <sagas/BattleCallbackData.hpp>
#include <sagas/FighterSourceData.hpp>
#include <sagas/FighterAttackData.hpp>
#include <limits>

#include <algorithm>
#include <cmath>

namespace sagas {
FighterAttributes fighter_attributes(FighterKind kind) {
    const auto& data=fighter_source_data.at(static_cast<std::size_t>(kind));
    FighterAttributes attr;
    attr.gravity=data.gravity; attr.tvel_base=data.terminal; attr.tvel_fast=data.fast;
    attr.jump_vel_y=80*data.jump_mul+data.jump_base;
    attr.walk_speed=80*data.walk_mul; attr.dash_speed=data.dash; attr.run_speed=data.run;
    attr.traction=data.traction; attr.air_accel=data.air_accel; attr.air_friction=data.air_friction;
    attr.air_speed_max_x=data.air_max; attr.height=data.height; attr.width=data.width;
    attr.jump_vel_x=data.jump_x; attr.size=data.size; attr.weight=data.weight;
    attr.knee_bend=static_cast<int>(data.kneebend); attr.jumps_max=static_cast<int>(data.jumps);
    attr.jump_height_mul=data.jump_mul; attr.jump_height_base=data.jump_base;
    attr.aerial_vel_x=data.aerial_x; attr.aerial_height=data.aerial_height;
    return attr;
}

FighterModelSpec fighter_model_spec(FighterKind kind) noexcept {
    switch (kind) {
        case FighterKind::Luigi:
            return {"llLuigiModelJointTreeDObjDesc", {0xffffff00U, 0U}};
        case FighterKind::Mario:
            return {"llMarioModelJointTreeDObjDesc", {0xffffff00U, 0U}};
        case FighterKind::Donkey:
            return {"llDonkeyModelJointTreeDObjDesc", {0xffffff80U, 0U}};
        case FighterKind::Link:
            return {"llLinkModelJointTreeDObjDesc", {0xfff9fffeU, 0U}};
        case FighterKind::Samus:
            return {"llSamusModelJointTreeDObjDesc", {0xfff803ffU, 0U}};
        case FighterKind::Captain:
            return {"llCaptainModelJointTreeDObjDesc", {0xffffff80U, 0U}};
        case FighterKind::Ness:
            return {"llNessModelJointTreeDObjDesc", {0xffffffc0U, 0U}};
        case FighterKind::Yoshi:
            return {"llYoshiModelJointTreeDObjDesc", {0xfbffffe0U, 0U}, true};
        case FighterKind::Kirby:
            return {"llKirbyModelJointTreeDObjDesc", {0xef7cffc0U, 0U}};
        case FighterKind::Fox:
            return {"llFoxModelJointTreeDObjDesc", {0xffffffc0U, 0U}};
        case FighterKind::Pikachu:
            return {"llPikachuModelJointTreeDObjDesc", {0xffffffc0U, 0U}};
        case FighterKind::Purin:
            return {"llPurinModelJointTreeDObjDesc", {0xeff9ff80U, 0U}};
        default:
            return {"llMarioModelJointTreeDObjDesc", {0xffffff00U, 0U}};
    }
}

void FighterPhysics::apply_gravity_clamp_tvel(FighterBody& body, float gravity, float tvel) noexcept {
    // ftPhysicsApplyGravityClampTVel
    body.vel_air.y -= gravity;
    if (body.vel_air.y < -tvel) body.vel_air.y = -tvel;
}

void FighterPhysics::clamp_ground_vel(FighterBody& body, float clamp) noexcept {
    if (body.vel_ground < -clamp) body.vel_ground = -clamp;
    else if (body.vel_ground > clamp) body.vel_ground = clamp;
}

void FighterPhysics::apply_ground_friction(FighterBody& body) noexcept {
    // ftPhysicsSetGroundVelFriction
    if (body.vel_ground < 0.0f) {
        body.vel_ground += body.attr.traction*body.floor_friction;
        if (body.vel_ground > 0.0f) body.vel_ground = 0.0f;
    } else {
        body.vel_ground -= body.attr.traction*body.floor_friction;
        if (body.vel_ground < 0.0f) body.vel_ground = 0.0f;
    }
}

void FighterPhysics::apply_air_vel_x_friction(FighterBody& body) noexcept {
    if (body.vel_air.x < 0.0f) {
        body.vel_air.x += body.attr.air_friction;
        if (body.vel_air.x >= 0.0f) body.vel_air.x = 0.0f;
    } else {
        body.vel_air.x -= body.attr.air_friction;
        if (body.vel_air.x <= 0.0f) body.vel_air.x = 0.0f;
    }
}

void FighterPhysics::clamp_air_vel_x_stick(FighterBody& body) noexcept {
    if (std::abs(body.stick_x) >= kStickMin) {
        body.vel_air.x += body.stick_x * body.attr.air_accel;
        if (body.vel_air.x < -body.attr.air_speed_max_x) body.vel_air.x = -body.attr.air_speed_max_x;
        else if (body.vel_air.x > body.attr.air_speed_max_x) body.vel_air.x = body.attr.air_speed_max_x;
    }
}

void FighterPhysics::apply_air_vel_drift(FighterBody& body) noexcept {
    // ftPhysicsApplyAirVelDrift
    if (body.fastfall) body.vel_air.y = -body.attr.tvel_fast;
    else apply_gravity_clamp_tvel(body, body.attr.gravity, body.attr.tvel_base);
    if (std::abs(body.vel_air.x) > body.attr.air_speed_max_x) {
        body.vel_air.x += body.vel_air.x >= 0.0f ? -1.0f : 1.0f;
        if (std::abs(body.vel_air.x) < body.attr.air_speed_max_x)
            body.vel_air.x = std::copysign(body.attr.air_speed_max_x, body.vel_air.x);
    } else {
        clamp_air_vel_x_stick(body);
        apply_air_vel_x_friction(body);
    }
}

void FighterPhysics::jump(FighterBody& body) noexcept {
    if (!body.grounded && body.jumps_used>=body.attr.jumps_max) return;
    body.aerial_jump=!body.grounded;
    body.jump_backward=body.stick_x*body.lr<-10;
    float force=std::max(53,body.jump_force);
    if (!body.aerial_jump && body.jump_button) {
        const float x=std::min(80,std::abs(body.stick_x));
        const float minimum=body.short_hop?36.f:63.f;
        force=(body.short_hop?9.f:17.f)*std::sqrt(1-x*x/6400)+minimum;
        if (x*x+force*force>6400) force=std::sqrt(6400-x*x);
        force=std::min(77.f,std::max(minimum,force));
        force=static_cast<int>(force);
    }
    float velocity=force*body.attr.jump_height_mul+body.attr.jump_height_base;
    if (body.aerial_jump) {
        velocity=body.attr.jump_vel_y*body.attr.aerial_height;
        if (body.jumps_used>=2 && (body.kind==FighterKind::Kirby || body.kind==FighterKind::Purin)) {
            constexpr std::array<float,4> kirby{60,52,47,40},purin{60,40,20,0};
            velocity=(body.kind==FighterKind::Kirby?kirby:purin)[body.jumps_used-2];
        }
    }
    body.jumps_used=body.grounded ? 1 : body.jumps_used+1;
    body.grounded=false; body.fastfall=false;
    body.vel_air.y=velocity;
    body.vel_air.x=body.stick_x*(body.aerial_jump?body.attr.aerial_vel_x:body.attr.jump_vel_x);
    body.vel_ground=0; body.status=FighterStatus::Jump; body.jump_frames=0; body.action_frame=0;
}

void FighterPhysics::tick(FighterBody& body,float ground_y) noexcept {
    const CollisionSegment floor{{-1000000,ground_y},{1000000,ground_y},0,0,false};
    tick(body,std::span<const CollisionSegment>(&floor,1));
}

void FighterPhysics::tick(FighterBody& body,std::span<const CollisionSegment> stage,const JumpMotion& motion) {
    if (body.status==FighterStatus::KO || body.status==FighterStatus::Captured) return;
    if (body.invincible>0) --body.invincible;
    if (body.hitlag>0) { --body.hitlag; return; }
    if (body.status==FighterStatus::Sleep) {
        body.jump_pressed=false;body.vel_ground=0;
        if (--body.sleep_tics<=0) {body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;body.action_frame=0;}
        return;
    }
    if (body.cliff_cooldown>0) --body.cliff_cooldown;
    if (body.drop_frames>0) --body.drop_frames;
    if (body.grounded) {
        constexpr std::array<float,16> friction{4,3,3,1,2,2,4,4,4,4,4,4,4,4,4,4};
        for (const auto& line:stage) {
            const float dx=line.b.x-line.a.x,dy=line.b.y-line.a.y;
            if (line.type!=0 || std::abs(dx)<.001f || body.position.x<std::min(line.a.x,line.b.x) || body.position.x>std::max(line.a.x,line.b.x)) continue;
            const float y=line.a.y+dy*(body.position.x-line.a.x)/dx;
            if (std::abs(y-body.position.y)>2) continue;
            body.floor_friction=friction[std::min(15U,line.flags&255U)];
            const float length=std::hypot(dx,dy);
            body.floor_tangent={std::abs(dx)/length,dy*std::copysign(1.f,dx)/length};
            break;
        }
    }
    const Vec3 before=body.position;
    const bool was_grounded=body.grounded;
    bool began_kneebend=false;
    if (body.jump_pressed && body.shield_stun==0 && body.status!=FighterStatus::SpecialFall && body.status!=FighterStatus::ShieldRoll && body.status!=FighterStatus::ShieldRelease && !fighter_is_down(body.status) && body.status!=FighterStatus::Hitstun && body.status!=FighterStatus::Attack && body.status!=FighterStatus::Special && body.status!=FighterStatus::Catch && body.status!=FighterStatus::CatchWait && body.status!=FighterStatus::Throw) {
        if (body.grounded && body.status!=FighterStatus::KneeBend) {
            began_kneebend=true;
            body.status=FighterStatus::KneeBend; body.jump_frames=0;
            body.short_hop=false; body.jump_force=body.stick_y;
        } else if (!body.grounded) jump(body);
    }
    if (body.grounded && body.stick_y<=-53 && body.tap_stick_y<4 &&
        (body.status==FighterStatus::Wait || body.status==FighterStatus::Crouch || body.status==FighterStatus::CrouchWait || body.status==FighterStatus::Walk || body.status==FighterStatus::Dash || body.status==FighterStatus::Run || body.status==FighterStatus::RunBrake || body.status==FighterStatus::Shield)) {
        for (const auto& line:stage) if (line.type==0 && line.pass_through &&
            body.position.x>=std::min(line.a.x,line.b.x) && body.position.x<=std::max(line.a.x,line.b.x)) {
            const float y=line.a.y+(line.b.y-line.a.y)*(body.position.x-line.a.x)/(line.b.x-line.a.x);
            if (std::abs(body.position.y-y)<2) {
                body.grounded=false; body.drop_frames=12; body.position.y-=1; body.vel_air.y=0;body.status=FighterStatus::Fall;body.action_frame=0;body.tap_stick_y=255;
                body.vel_air.x=std::clamp(body.vel_air.x,-body.attr.air_speed_max_x,body.attr.air_speed_max_x);
                break;
            }
        }
    }
    if (body.grounded && body.stick_y<=-53 &&
        (body.status==FighterStatus::Wait || body.status==FighterStatus::Walk || body.status==FighterStatus::CrouchEnd)) {
        body.status=FighterStatus::Crouch;body.action_frame=0;
    }
    if (body.grounded && body.status==FighterStatus::CrouchWait && body.stick_y>=-49) {
        body.status=FighterStatus::CrouchEnd;body.action_frame=0;
    }
    if (body.status==FighterStatus::KneeBend) {
        if (!began_kneebend) ++body.jump_frames;
        if (body.jump_button && body.jump_released && body.jump_frames<=3) body.short_hop=true;
        body.jump_force=std::max(body.jump_force,body.stick_y);
        if (body.jump_frames>=body.attr.knee_bend) jump(body);
    }
    if (body.grounded) {
        if (body.status==FighterStatus::Turn && !body.turn_flipped) {
            const auto clip=fighter_source_data[static_cast<unsigned>(body.kind)].turn;
            for (const auto& flag:source_motion_flags) if (flag.kind==static_cast<unsigned>(body.kind) && flag.motion==clip && flag.value && body.action_frame>=static_cast<int>(flag.frame)) {
                body.lr=-body.lr;body.vel_ground=-body.vel_ground;body.turn_flipped=true;
                if (body.turn_dash && body.stick_x*body.lr>=56) {
                    body.status=FighterStatus::Dash;body.action_frame=0;body.vel_ground=body.attr.dash_speed;body.tap_stick_x=255;
                }
                break;
            }
        }
        if (body.status==FighterStatus::Land && !body.landing_motion && --body.land_frames<=0) body.status=FighterStatus::Wait;
        const bool locked=fighter_is_down(body.status) || body.status==FighterStatus::Turn || body.status==FighterStatus::Crouch || body.status==FighterStatus::CrouchWait || body.status==FighterStatus::CrouchEnd || body.status==FighterStatus::KneeBend || body.status==FighterStatus::Attack ||
                          body.status==FighterStatus::Special || body.status==FighterStatus::ShieldRelease || body.status==FighterStatus::ShieldRoll || body.status==FighterStatus::Shield || body.status==FighterStatus::Hitstun || body.status==FighterStatus::Land ||
                          body.status==FighterStatus::Catch || body.status==FighterStatus::CatchWait || body.status==FighterStatus::Throw;
        if (!locked && body.status==FighterStatus::Run && body.stick_x*body.lr<44) {body.status=FighterStatus::RunBrake;body.action_frame=0;}
        if (!locked && body.status==FighterStatus::Dash) {
            const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
            if (body.action_frame>=data.dash_to_run && body.action_frame<data.dash_to_run+1 && body.stick_x*body.lr>=44) {
                body.status=FighterStatus::Run;body.action_frame=0;body.vel_ground=body.attr.run_speed;
            } else if (body.action_frame>=7) body.vel_ground=std::max(0.f,body.vel_ground-data.dash_decel);
        } else if (!locked && body.status==FighterStatus::RunBrake) {
            body.vel_ground=std::max(0.f,body.vel_ground-body.attr.traction*1.25f);
            if (body.vel_ground==0) body.status=FighterStatus::Wait;
        } else if (!locked && std::abs(body.stick_x)>=kStickMin) {
            if (body.stick_x*body.lr<0) {
                body.status=FighterStatus::Turn;body.action_frame=0;body.turn_flipped=false;
                body.turn_dash=std::abs(body.stick_x)>=56 && body.tap_stick_x<3;
                apply_ground_friction(body);
            } else if (body.status==FighterStatus::Run) body.vel_ground=body.attr.run_speed;
            else if (std::abs(body.stick_x)>=56 && body.tap_stick_x<3) {
                body.status=FighterStatus::Dash;body.action_frame=0;body.vel_ground=body.attr.dash_speed;body.tap_stick_x=255;
            } else {
                const float target=body.attr.walk_speed*std::abs(body.stick_x)/80.0f;
                body.vel_ground=body.vel_ground<target?target:std::max(target,body.vel_ground-body.attr.traction);
                body.status=FighterStatus::Walk;
            }
        } else {
            apply_ground_friction(body);
            if (!locked) body.status=FighterStatus::Wait;
        }
        body.vel_air.z=0;
        if ((body.status==FighterStatus::Attack || body.status==FighterStatus::Special || body.status==FighterStatus::CatchWait || body.status==FighterStatus::Throw || body.status==FighterStatus::DownRoll || body.status==FighterStatus::ShieldRoll || body.status==FighterStatus::DownAttack) && motion) {
            if (const auto authored=motion(body)) {
                body.vel_ground=authored->x*body.lr;body.vel_air.z=authored->z;
                if (body.status==FighterStatus::Special && body.kind==FighterKind::Kirby && body.special_index%3==1 && authored->y>0) {body.grounded=false;body.vel_air.y=authored->y;body.jumps_used=std::max(1,body.jumps_used);}
            }
        }
        body.vel_air.x=body.vel_ground*body.lr*body.floor_tangent.x;
        if (body.grounded) body.vel_air.y=body.vel_ground*body.lr*body.floor_tangent.y;
    } else {

        const bool authored=body.aerial_jump && body.status==FighterStatus::Jump &&
                            (body.kind==FighterKind::Ness || body.kind==FighterKind::Yoshi) && motion;
        const auto root_velocity=authored?motion(body):std::optional<Vec3>{};
        if (!authored && body.stick_y<=-53 && body.tap_stick_y<4 && body.vel_air.y<0) {body.fastfall=true;body.tap_stick_y=255;}
        if (body.status==FighterStatus::Hitstun) apply_gravity_clamp_tvel(body,body.attr.gravity,body.attr.tvel_base);
        else {
            if (body.aerial_jump && body.status==FighterStatus::Jump &&
                (body.kind==FighterKind::Kirby || body.kind==FighterKind::Purin)) {
                if (body.fastfall) body.vel_air.y=-body.attr.tvel_fast;
                else apply_gravity_clamp_tvel(body,body.attr.gravity,body.attr.tvel_base);
                if (std::abs(body.vel_air.x)>body.attr.air_speed_max_x)
                    body.vel_air.x=std::copysign(std::max(body.attr.air_speed_max_x,std::abs(body.vel_air.x)-1),body.vel_air.x);
                else if (std::abs(body.stick_x)>=kStickMin)
                    body.vel_air.x=std::clamp(body.vel_air.x+body.stick_x*body.attr.air_accel*.8f,
                                            -body.attr.air_speed_max_x*.8f,body.attr.air_speed_max_x*.8f);
                apply_air_vel_x_friction(body);
            } else apply_air_vel_drift(body);
            if (root_velocity) {
                body.vel_air.y=root_velocity->y;
                // Ness adds authored horizontal drift to controlled velocity;
                // Yoshi only uses the vertical/depth components.
            } else if (authored) body.status=FighterStatus::Fall;
        }
        if (root_velocity && body.kind==FighterKind::Ness) body.position.x+=root_velocity->x;
        if (body.status==FighterStatus::Special) {
            if (body.kind==FighterKind::Pikachu && body.special_index%3==1) {
                if (body.special_phase==3) body.vel_air={body.special_velocity.x,body.special_velocity.y,0};
                else if (body.special_phase==0) body.vel_air.y=std::max(-body.attr.tvel_base,body.vel_air.y+body.attr.gravity-.8f);
            } else if (body.kind==FighterKind::Captain && body.special_index%3==1 && body.special_phase==4) body.vel_air={};
            else if (body.kind==FighterKind::Fox && body.special_index%3==1) {
                if (body.special_phase==3) {
                    const float speed=std::max(0.f,115.f-std::max(0,body.action_frame-2)*3.03571438789f);
                    body.vel_air={body.special_velocity.x*speed/115,body.special_velocity.y*speed/115,0};
                } else if (body.special_phase!=2) body.vel_air={};
            } else if (body.kind==FighterKind::Fox && body.special_index%3==2) {
                body.vel_air.x*=.8f;body.vel_air.y=body.special_tics<=4?0:std::max(-body.attr.tvel_base,body.vel_air.y+body.attr.gravity-.8f);
            } else if (body.kind==FighterKind::Captain && body.special_index%3==0) {
                // Falcon Punch uses a one-shot angled boost, not TransN in air.
                const int flag=FighterCombat::special_flag(body,2);
                if (FighterCombat::special_flag(body,1) && !body.special_second) {
                    body.special_second=true;
                    const float angle=std::copysign(std::clamp(std::abs(body.stick_y)-10,0,40)*(.5235987756f/40),float(body.stick_y));
                    body.vel_air={std::cos(angle)*65*body.lr,std::sin(angle)*65,0};
                } else if (flag<2) {
                    body.vel_air.y+=body.attr.gravity;
                    if (flag==1) {body.vel_air.x*=.92f;body.vel_air.y*=.92f;}
                }
            } else if (body.kind==FighterKind::Kirby && body.special_index%3==1) {
                if (body.special_phase==1) body.vel_air.y=-body.attr.tvel_fast;
                else if (motion) if (const auto authored=motion(body)) body.vel_air.y=authored->y;
            }

        }
        if (body.status==FighterStatus::Jump) ++body.jump_frames;
    }
    // ftMainProcPhysicsMap keeps knockback separate from controlled velocity.
    // Air knockback decays by 1.7 per tick; grounded knockback uses traction.
    if (body.grounded) {
        const float speed=std::max(0.0f,std::abs(body.vel_damage.x)-body.floor_friction*body.attr.traction*.25f);
        body.vel_damage.x=std::copysign(speed,body.vel_damage.x);
        body.vel_damage.y=0;
    } else {
        const float speed=std::hypot(body.vel_damage.x,body.vel_damage.y);
        if (speed>0) {
            const float decay=std::max(0.0f,speed-1.7f)/speed;
            body.vel_damage.x*=decay; body.vel_damage.y*=decay;
        }
    }
    const float velocity_x=body.vel_air.x+body.vel_damage.x;
    const float velocity_y=body.vel_air.y+body.vel_damage.y;
    body.position.x+=velocity_x; body.position.y+=velocity_y;body.position.z+=body.vel_air.z;
    const bool follows_floor=body.grounded;
    body.grounded=false;
    float floor=-std::numeric_limits<float>::infinity();
    for (const auto& line:stage) {
        if (line.type==0 && std::abs(line.b.x-line.a.x)>.001f) {
            if (line.pass_through && body.drop_frames) continue;
            const float x=body.position.x;
            if (x<std::min(line.a.x,line.b.x) || x>std::max(line.a.x,line.b.x)) continue;
            const float y=line.a.y+(line.b.y-line.a.y)*(x-line.a.x)/(line.b.x-line.a.x);
            const float old_y=line.a.y+(line.b.y-line.a.y)*(before.x-line.a.x)/(line.b.x-line.a.x);
            if ((velocity_y<=0 || follows_floor) && before.y>=old_y-2 &&
                (body.position.y<=y || (was_grounded && std::abs(y-body.position.y)<100))) floor=std::max(floor,y);
        } else if (line.type==1 && std::abs(line.b.x-line.a.x)>.001f) {
            const float x=body.position.x;
            if (x<std::min(line.a.x,line.b.x) || x>std::max(line.a.x,line.b.x)) continue;
            const float y=line.a.y+(line.b.y-line.a.y)*(x-line.a.x)/(line.b.x-line.a.x);
            if (velocity_y>0 && before.y+body.attr.height<=y && body.position.y+body.attr.height>=y) {
                body.position.y=y-body.attr.height; body.vel_air.y=body.vel_damage.y=0;
            }
        } else if (line.type>=2 && std::abs(line.b.y-line.a.y)>.001f) {
            if (body.position.y+body.attr.height<std::min(line.a.y,line.b.y) || body.position.y>=std::max(line.a.y,line.b.y)-.01f) continue;
            const float x=line.a.x+(line.b.x-line.a.x)*(body.position.y-line.a.y)/(line.b.y-line.a.y);
            const float side=velocity_x>=0?body.attr.width:-body.attr.width;
            if ((before.x+side-x)*(body.position.x+side-x)<=0) {
                body.position.x=x-side; body.vel_air.x=body.vel_ground=body.vel_damage.x=0;
            }
        }
    }
    if (std::isfinite(floor)) {
        body.position.y=floor; body.vel_air.y=body.vel_damage.y=0; body.grounded=true; body.fastfall=false; body.jumps_used=0;
        if (!was_grounded) {
            // Landing transfers world velocity back into facing-relative ground velocity.
            body.vel_ground=body.vel_air.x*body.lr;
            if (body.status==FighterStatus::Tumble || (body.status==FighterStatus::Hitstun && body.damage_tumble)) {
                body.status=FighterStatus::DownBounce;body.action_frame=0;body.hitstun=0;
                body.down_motion=fighter_source_data[static_cast<unsigned>(body.kind)].down[body.down_face];
                if (body.shield_tics<20) {
                    const unsigned tech=std::abs(body.stick_x)<20?0:body.stick_x*body.lr>=0?1:2;
                    body.status=tech?FighterStatus::DownRoll:FighterStatus::DownStand;
                    body.down_motion=fighter_source_data[static_cast<unsigned>(body.kind)].tech[tech];
                    body.shield_tics=255;
                }
                body.down_attack_buffer=0;body.down_wait=180;body.vel_ground=0;
                body.attack_motion=0;body.aerial_attack=-1;
            } else if (body.status==FighterStatus::Attack && body.aerial_attack>=0) {
                int flag=0;
                for (const auto& event:source_motion_flags)
                    if (event.kind==static_cast<unsigned>(body.kind) && event.motion==body.attack_motion && event.frame<=static_cast<unsigned>(body.action_frame)) flag=event.value;
                const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
                body.landing_motion=flag && body.shield_tics>10?data.landing_air[body.aerial_attack]:0;
                body.land_frames=4;
                body.landing_speed=flag && body.shield_tics>10 && !body.landing_motion?flag*.01f:1.f;
                if (flag && body.shield_tics>10 && !body.landing_motion) body.landing_motion=data.landing;
                body.aerial_attack=-1;body.attack_motion=0;body.jab_stage=0;body.hit_mask=0;
                body.status=FighterStatus::Land;body.action_frame=0;
            } else if (body.status==FighterStatus::Special && body.kind==FighterKind::Fox && body.special_index%3==0) {
                body.status=FighterStatus::Land;body.land_frames=4;body.landing_motion=0;body.action_frame=0;
                body.special_projectile=true; // Landing cannot execute the grounded shot script.
            } else if (body.status==FighterStatus::Special && body.kind==FighterKind::Kirby && body.special_index%3==1) {
                body.special_phase=2;body.special_index=1;
                body.special_motion=fighter_source_data[static_cast<unsigned>(body.kind)].special_end[1];
                body.action_frame=0;body.special_projectile=false;
            } else if (body.status==FighterStatus::Special && (body.special_index%3!=1 || body.kind==FighterKind::Purin)) {
                const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
                body.special_index%=3;
                body.special_motion=body.special_phase==0?data.special_start[body.special_index]:body.special_phase==1?data.special_loop[body.special_index]:body.special_phase==4?data.special_hit[body.special_index]:data.special_end[body.special_index];
            } else if (body.status==FighterStatus::SpecialFall) {
                body.status=FighterStatus::Land;body.land_frames=4;body.landing_motion=fighter_source_data[static_cast<unsigned>(body.kind)].landing;
                body.landing_speed=body.kind==FighterKind::Pikachu?.4f:.65f;body.action_frame=0;
            } else if (body.status!=FighterStatus::Attack && body.status!=FighterStatus::Hitstun) {
                body.status=FighterStatus::Land; body.land_frames=4;body.landing_motion=0;body.action_frame=0;
            }
        }
    } else if (fighter_is_down(body.status) || body.status==FighterStatus::Wait || body.status==FighterStatus::Crouch || body.status==FighterStatus::CrouchWait || body.status==FighterStatus::CrouchEnd || body.status==FighterStatus::Walk || body.status==FighterStatus::Dash || body.status==FighterStatus::Run || body.status==FighterStatus::RunBrake)
    {
        body.status=FighterStatus::Fall;body.action_frame=0;
        body.vel_air.x=std::clamp(body.vel_air.x,-body.attr.air_speed_max_x,body.attr.air_speed_max_x);
    }
    if (was_grounded && !body.grounded && body.status==FighterStatus::Special && (body.special_index%3!=1 || body.kind==FighterKind::Purin)) {
        const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];body.special_index=body.special_index%3+3;
        body.special_motion=body.special_phase==0?data.special_start[body.special_index]:body.special_phase==1?data.special_loop[body.special_index]:body.special_phase==4?data.special_hit[body.special_index]:data.special_end[body.special_index];
    }
    if (was_grounded && !body.grounded && body.jumps_used==0) body.jumps_used=1;
    body.jump_pressed=false;
}

bool FighterPhysics::try_ledge(FighterBody& body,Vec3 before,std::span<const CollisionSegment> stage,std::span<const FighterBody> others) {
    if (body.grounded || body.cliff_cooldown || body.status==FighterStatus::Hitstun || body.status==FighterStatus::Attack || body.position.y>=before.y) return false;
    const auto& box=fighter_source_data[static_cast<unsigned>(body.kind)].cliff_box;
    for (const auto& line:stage) {
        if (line.type!=0 || !(line.flags&0x8000) || std::abs(line.b.x-line.a.x)<.001f) continue;
        const float x=body.position.x+body.lr*box[0];
        if (x<std::min(line.a.x,line.b.x) || x>std::max(line.a.x,line.b.x)) continue;
        const float y=line.a.y+(line.b.y-line.a.y)*(x-line.a.x)/(line.b.x-line.a.x);
        if (before.y+box[1]<y || body.position.y+box[1]>y) continue;
        Vec2 edge=body.lr>0?(line.a.x<line.b.x?line.a:line.b):(line.a.x>line.b.x?line.a:line.b);
        for (const auto& part:stage) if (part.type==0 && part.line_id==line.line_id)
            for (const auto point:{part.a,part.b})
                if ((body.lr>0 && point.x<edge.x) || (body.lr<0 && point.x>edge.x)) edge=point;
        if ((x-edge.x)*body.lr>=800) continue;
        bool occupied=false;
        for (const auto& other:others) if (&other!=&body && other.cliff_line==line.line_id && other.lr==body.lr &&
            (other.status==FighterStatus::CliffCatch || other.status==FighterStatus::CliffWait || other.status==FighterStatus::CliffClimb)) occupied=true;
        if (occupied) continue;
        body.cliff_edge=edge;body.cliff_line=line.line_id;body.status=FighterStatus::CliffCatch;
        body.action_frame=0;body.vel_air={};body.vel_damage={};body.vel_ground=0;body.jumps_used=1;
        body.fastfall=false;body.cliff_neutral=false;body.invincible=60;
        return true;
    }
    return false;
}

void FighterCombat::advance_guard(FighterBody& body,bool animation_ended) {
    const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
    if (body.status==FighterStatus::ShieldRoll) {
        if (!body.turn_flipped) for (const auto& event:source_motion_flags)
            if (event.kind==static_cast<unsigned>(body.kind) && event.motion==body.guard_motion && event.value && event.frame<=static_cast<unsigned>(body.action_frame)) {
                body.lr=-body.lr;body.vel_ground=-body.vel_ground;body.turn_flipped=true;break;
            }
        if (animation_ended) {body.status=FighterStatus::Wait;body.action_frame=0;body.vel_ground=0;body.vel_air={};}
        return;
    }
    if (body.status==FighterStatus::ShieldRelease) {
        if (animation_ended) {body.status=FighterStatus::Wait;body.action_frame=0;}
        return;
    }
    if (body.status==FighterStatus::Shield) {
        if (body.shield_stun>0) {--body.shield_stun;return;}
        if (std::abs(body.stick_x)>=56 && body.tap_stick_x<4) {
            body.guard_motion=data.guard[body.stick_x*body.lr>=0?2:3];
            body.status=FighterStatus::ShieldRoll;body.action_frame=0;body.turn_flipped=false;return;
        }
        if ((!body.shield_held || body.shield<=0) && body.action_frame>=8) {
            body.status=FighterStatus::ShieldRelease;body.guard_motion=data.guard[1];body.action_frame=0;
        }
        return;
    }
    const bool available=body.status==FighterStatus::Wait || body.status==FighterStatus::Walk || body.status==FighterStatus::Dash ||
        body.status==FighterStatus::Run || body.status==FighterStatus::RunBrake || body.status==FighterStatus::CrouchWait;
    // Landing keeps its recovery frames; a Z-cancel tap cannot overwrite it.
    if (available && body.grounded && body.shield_held && body.shield>0) {
        body.status=FighterStatus::Shield;body.guard_motion=data.guard[0];body.action_frame=0;
    }
}

void FighterCombat::advance_down(FighterBody& body,bool attack,bool stand,bool animation_ended) {
    if (!fighter_is_down(body.status) || body.hitlag) return;
    const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
    if (body.status==FighterStatus::DownBounce) {
        body.down_attack_buffer=std::max(0,body.down_attack_buffer-1);
        if (attack) body.down_attack_buffer=60;
        if (!animation_ended) return;
        attack=body.down_attack_buffer>0;
        body.status=FighterStatus::DownWait;body.down_wait=180;
        // Keep the final bounce pose while lying down (source motion ID -2).
    } else if (body.status!=FighterStatus::DownWait) {
        if (animation_ended) {
            body.status=FighterStatus::Wait;body.action_frame=0;body.recovery_invulnerable=false;
        }
        return;
    }
    const float angle=std::atan2(static_cast<float>(std::abs(body.stick_y)),static_cast<float>(std::abs(body.stick_x)));
    if (attack) {
        body.status=FighterStatus::DownAttack;body.down_motion=data.down[10+body.down_face];
        body.hit_mask=0;body.hit_group_masks.fill(0);body.hit_group_epochs.fill(~0U);
    } else if (std::abs(body.stick_x)>=20 && angle<.872664626f) {
        body.status=FighterStatus::DownRoll;body.down_motion=data.down[(body.stick_x*body.lr>=0?6:8)+body.down_face];
    } else if (stand || (body.stick_y>=20 && angle>=.872664626f) || --body.down_wait<=0) {
        body.status=FighterStatus::DownStand;body.down_motion=data.down[2+body.down_face];
    } else return;
    body.action_frame=0;body.vel_ground=0;body.jump_pressed=false;
}

int FighterCombat::special_flag(const FighterBody& body,unsigned flag) {
    int value=0;
    const unsigned event=special_event_motion(body);
    if (flag==1) {
        for (const auto& f:source_motion_flags) if (f.kind==static_cast<unsigned>(body.kind) && f.motion==event && f.frame<=static_cast<unsigned>(body.action_frame)) value=f.value;
    } else for (const auto& f:source_special_flags) if (f.kind==static_cast<unsigned>(body.kind) && f.motion==event && f.flag==flag && f.frame<=static_cast<unsigned>(body.action_frame)) value=f.value;
    return value;
}

unsigned FighterCombat::special_event_motion(const FighterBody& body) {
    return fighter_source_data[static_cast<unsigned>(body.kind)].special_events[std::min(4U,body.special_phase)][body.special_index];
}

bool FighterCombat::start_special(FighterBody& body,bool pressed) {
    if (!pressed || body.hitlag || (body.status!=FighterStatus::Wait && body.status!=FighterStatus::Walk &&
        body.status!=FighterStatus::Dash && body.status!=FighterStatus::Run && body.status!=FighterStatus::CrouchWait &&
        body.status!=FighterStatus::Crouch && body.status!=FighterStatus::Jump && body.status!=FighterStatus::Fall && body.status!=FighterStatus::Tumble)) return false;
    const unsigned direction=body.stick_y>=40?1:body.stick_y<=-40?2:0;
    const unsigned index=(body.grounded?0:3)+direction;
    const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
    if (!data.special_start[index]) return false;
    body.special_index=index;body.special_motion=data.special_start[index];body.special_phase=0;body.special_tics=0;
    body.special_second=false;body.special_direction_checked=false;
    body.special_projectile=false;body.status=FighterStatus::Special;body.action_frame=0;
    body.attack_motion=0;body.hit_mask=0;body.hit_group_masks.fill(0);body.hit_group_epochs.fill(~0U);body.fastfall=false;
    if (direction==0 && std::abs(body.stick_x)>=20) body.lr=body.stick_x>0?1:-1;
    if (direction==1 && body.kind==FighterKind::Captain) {body.grounded=false;body.vel_air={};body.jumps_used=body.attr.jumps_max;}
    if (direction==1 && body.kind==FighterKind::Donkey && !body.grounded) body.vel_air.y=18.f;
    return true;
}
void FighterCombat::advance_special(FighterBody& body,bool pressed,bool animation_ended) {
    if (body.status!=FighterStatus::Special || body.hitlag) return;
    ++body.special_tics;
    const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
    const auto index=body.special_index;
    if (body.kind==FighterKind::Kirby && index%3==1) {
        if (animation_ended && body.special_phase==0) {
            body.special_phase=1;body.special_motion=data.special_loop[index];body.action_frame=0;
            body.grounded=false;body.vel_air.y=-body.attr.tvel_fast;body.jumps_used=body.attr.jumps_max;
        } else if (animation_ended && body.special_phase==2) {body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;body.action_frame=0;}
        return;
    }
    if (body.kind==FighterKind::Kirby && index%3==0 && body.capture_target>=0) return;
    if (body.kind==FighterKind::Fox && index%3==0 && pressed && special_flag(body,1)) {
        body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;
        start_special(body,true);return;
    }
    if (body.kind==FighterKind::Fox && index%3==2 && body.special_tics>=4 && body.jump_pressed && (body.grounded || body.jumps_used<body.attr.jumps_max)) {
        body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;return;
    }
    if (body.kind==FighterKind::Captain && index%3==1) {
        if (body.special_phase==4) return; // Capture links own the release event.
        if (animation_ended) {body.status=body.special_phase==2?FighterStatus::Fall:FighterStatus::SpecialFall;body.action_frame=0;}
        return;
    }
    if (body.kind==FighterKind::Pikachu && index%3==1) {
        const auto zip=[&] {
            float magnitude=std::hypot(body.stick_x,body.stick_y);
            const float angle=magnitude>60?std::atan2(float(body.stick_y),float(body.stick_x)):1.57079632679f;
            magnitude=magnitude>60?std::min(80.f,magnitude):80.f;
            const float speed=(3*magnitude+90)*(body.special_second?.9f:1.f);
            body.special_velocity={std::cos(angle)*speed,std::sin(angle)*speed};
            body.grounded=false;body.special_index=4;body.special_motion=data.special_active[4];body.special_phase=3;body.action_frame=0;
            body.jumps_used=body.attr.jumps_max;body.special_direction_checked=false;
        };
        if (body.special_phase==0) {if (body.special_tics>=20) zip();return;}
        if (body.special_phase==3) {
            if (body.action_frame>=5) {body.special_phase=2;body.special_motion=data.special_end[4];body.action_frame=0;body.vel_air={body.special_velocity.x*.2f,body.special_velocity.y*.2f,0};}
            return;
        }
        if (!body.special_second && !body.special_direction_checked) for (const auto& flag:source_motion_flags)
            if (flag.kind==static_cast<unsigned>(body.kind) && flag.motion==special_event_motion(body) && flag.value==1 && flag.frame<=static_cast<unsigned>(body.action_frame)) {
                body.special_direction_checked=true;
                const float mag=std::hypot(body.stick_x,body.stick_y),speed=std::hypot(body.special_velocity.x,body.special_velocity.y);
                if (mag>=60 && (body.stick_x*body.special_velocity.x+body.stick_y*body.special_velocity.y)/(mag*speed)<std::cos(.7330383f)) {body.special_second=true;zip();}
                return;
            }
        if (animation_ended) {body.status=body.grounded?FighterStatus::Wait:FighterStatus::SpecialFall;body.action_frame=0;}
        return;
    }
    if (body.kind==FighterKind::Pikachu && index%3==2 && (body.special_phase==1 || body.special_phase==4)) {
        for (const auto& flag:source_motion_flags) if (flag.kind==static_cast<unsigned>(body.kind) && flag.motion==special_event_motion(body) && flag.value && flag.frame<=static_cast<unsigned>(body.action_frame)) {
            body.special_phase=2;body.special_motion=data.special_end[index];body.action_frame=0;break;
        }
        return;
    }
    if (body.kind==FighterKind::Fox && index%3==1) {
        if (body.special_tics==35) {
            const float angle=std::hypot(body.stick_x,body.stick_y)>=45?std::atan2(static_cast<float>(body.stick_y),static_cast<float>(body.stick_x)):1.57079632679f;
            body.special_velocity={115*std::cos(angle),115*std::sin(angle)};
            body.grounded=false;body.special_index=4;body.special_phase=3;body.special_motion=data.special_active[4];body.action_frame=0;
            body.jumps_used=body.attr.jumps_max;return;
        }
        if (body.special_phase==3) {
            if (body.action_frame>=30) {body.special_phase=2;body.special_motion=data.special_end[4];body.action_frame=0;}
            return;
        }
        if (body.special_phase==1) return;
    }
    if (body.kind==FighterKind::Fox && index%3==2 && body.special_phase==1 && body.special_tics>=18 && !body.special_held) {
        body.special_phase=2;body.special_motion=data.special_end[index];body.action_frame=0;return;
    }
    if (body.special_phase==0 && animation_ended && data.special_loop[index]) {
        body.special_phase=1;body.special_motion=data.special_loop[index];body.action_frame=0;
        if (body.kind==FighterKind::Fox && index%3==1) body.special_tics=0;
        return;
    }
    if (body.special_phase==1 && (pressed || body.special_tics>=120)) {
        if (data.special_end[index]) {body.special_phase=2;body.special_motion=data.special_end[index];body.action_frame=0;return;}
        animation_ended=true;
    } else if (body.special_phase==1) return;
    if (animation_ended) {body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;body.action_frame=0;}
}

void FighterCombat::buffer_aerial(FighterBody& body,bool pressed) {
    if (!body.hitlag && body.aerial_buffer>0) --body.aerial_buffer;
    if (pressed && (body.status==FighterStatus::KneeBend || (body.grounded && body.jump_pressed)))
        body.aerial_buffer=body.attr.knee_bend+3;
}

bool FighterCombat::start_aerial(FighterBody& body,bool pressed) {
    if ((!pressed && body.aerial_buffer<=0) || body.hitlag || body.grounded ||
        (body.status!=FighterStatus::Jump && body.status!=FighterStatus::Fall && body.status!=FighterStatus::Tumble)) return false;
    int direction=0;
    if (std::abs(body.stick_x)>=20 || std::abs(body.stick_y)>=20) {
        const float angle=std::atan2(static_cast<float>(body.stick_y),static_cast<float>(std::abs(body.stick_x)));
        direction=angle>.87266463f?3:angle<-.87266463f?4:body.stick_x*body.lr>=0?1:2;
    }
    body.aerial_buffer=0;body.aerial_attack=direction;
    body.attack_motion=fighter_source_data[static_cast<unsigned>(body.kind)].attack_air[direction];
    body.status=FighterStatus::Attack;body.action_frame=0;body.hit_mask=0;body.attack_epoch=~0U;body.hit_group_epochs.fill(~0U);
    body.jab_stage=0;body.jab_followup_left=0;body.shield_tics=255;
    return true;
}

bool FighterCombat::start_grab(FighterBody& body,bool pressed) {
    if (body.shield_stun>0 || !pressed || body.hitlag || !body.grounded ||
        (body.status!=FighterStatus::Wait && body.status!=FighterStatus::Crouch && body.status!=FighterStatus::CrouchWait && body.status!=FighterStatus::CrouchEnd && body.status!=FighterStatus::Walk && body.status!=FighterStatus::Dash &&
         body.status!=FighterStatus::Run && body.status!=FighterStatus::Shield)) return false;
    body.status=FighterStatus::Catch;body.action_frame=0;body.hit_mask=0;body.attack_epoch=~0U;body.hit_group_epochs.fill(~0U);
    body.attack_motion=0;body.jab_stage=0;body.aerial_attack=-1;
    return true;
}

bool FighterCombat::start_dash_attack(FighterBody& body,bool pressed) {
    if (!pressed || body.hitlag || !body.grounded || (body.status!=FighterStatus::Run && body.status!=FighterStatus::Dash)) return false;
    body.attack_motion=fighter_source_data[static_cast<unsigned>(body.kind)].dash_attack;
    body.status=FighterStatus::Attack;body.action_frame=0;body.hit_mask=0;body.hit_group_epochs.fill(~0U);
    body.jab_stage=0;body.jab_followup_left=0;
    return true;
}

bool FighterCombat::start_tilt(FighterBody& body,bool pressed) {
    if (!pressed || body.hitlag || !body.grounded ||
        (body.status!=FighterStatus::Wait && body.status!=FighterStatus::Walk && body.status!=FighterStatus::Crouch &&
         body.status!=FighterStatus::CrouchWait && body.status!=FighterStatus::CrouchEnd)) return false;
    const auto& tilts=fighter_source_data[static_cast<unsigned>(body.kind)].tilt;
    const float angle=std::atan2(static_cast<float>(body.stick_y),static_cast<float>(std::abs(body.stick_x)));
    unsigned clip=0;
    if (body.stick_y>=20 && angle>.87266463f) clip=tilts[5];
    else if (body.stick_y<=-20 && angle<-.87266463f) clip=tilts[6];
    else if (body.stick_x*body.lr>=20 && std::abs(angle)<=.87266463f) {
        unsigned index=2;
        if (tilts[1]) index=angle>.5235988f?0:angle>.17453294f?1:angle<-.5235988f?4:angle<-.17453294f?3:2;
        else if (tilts[0]) index=angle>.296706f?0:angle<-.296706f?4:2;
        clip=tilts[index];
    }
    if (!clip) return false;
    body.attack_motion=clip;body.status=FighterStatus::Attack;body.action_frame=0;
    body.hit_mask=0;body.hit_group_epochs.fill(~0U);body.jab_stage=0;body.jab_followup_left=0;
    return true;
}

void FighterCombat::buffer_smash(FighterBody& body,bool pressed) {
    if (!body.hitlag && body.smash_buffer>0) --body.smash_buffer;
    if (!pressed || !body.grounded) return;
    int direction=-1;
    if (body.tap_stick_y<4 && std::abs(body.stick_y)>=53) direction=body.stick_y>0?1:2;
    else if (body.tap_stick_x<3 && std::abs(body.stick_x)>=56) direction=0;
    if (direction<0) return;
    body.smash_buffer=3;body.buffered_smash=direction;body.buffered_facing=direction==0?(body.stick_x>0?1:-1):body.lr;
}

bool FighterCombat::start_smash(FighterBody& body,bool pressed) {
    if ((!pressed && body.smash_buffer<=0) || body.hitlag || !body.grounded || (body.status!=FighterStatus::Wait && body.status!=FighterStatus::Crouch && body.status!=FighterStatus::CrouchWait && body.status!=FighterStatus::CrouchEnd && body.status!=FighterStatus::Walk && body.status!=FighterStatus::Dash && body.status!=FighterStatus::Run && body.status!=FighterStatus::RunBrake && body.status!=FighterStatus::KneeBend)) return false;
    int direction=-1;
    if (body.tap_stick_y<4 && body.stick_y>=53) direction=1;
    else if (body.tap_stick_y<4 && body.stick_y<=-53) direction=2;
    else if (body.tap_stick_x<3 && std::abs(body.stick_x)>=56) {direction=0;body.lr=body.stick_x>0?1:-1;}
    if (direction<0 && body.smash_buffer>0) {direction=body.buffered_smash;body.lr=body.buffered_facing;}
    if (direction<0 || (body.status==FighterStatus::KneeBend && direction!=1)) return false;
    body.smash_buffer=0;
    body.attack_motion=fighter_source_data[static_cast<unsigned>(body.kind)].smash[direction];
    body.status=FighterStatus::Attack;body.action_frame=0;body.hit_mask=0;body.attack_epoch=~0U;body.hit_group_epochs.fill(~0U);
    body.jab_stage=0;body.jab_followup_left=0;body.vel_ground=0;
    return true;
}

void FighterCombat::advance_jab(FighterBody& body,bool pressed,bool animation_ended,bool released) {
    if (body.hitlag) return;
    const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
    if (body.attack_motion) {
        if (animation_ended) {body.aerial_attack=-1;body.attack_motion=0;body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;}
        return;
    }
    if (body.status==FighterStatus::Attack && (pressed || released)) {
        ++body.rapid_inputs;body.rapid_continue=true;
    }
    const unsigned next=body.jab_stage==1 ? (body.kind==FighterKind::Pikachu?1:(data.jab2?2:0)) :
                        body.jab_stage==2 && data.jab3?3:0;
    const auto start=[&](unsigned stage) {
        body.jab_stage=stage; body.jab_queued=false;
        body.jab_followup_left=stage==1?static_cast<int>(data.jab_window):stage==2?24:0;
        body.status=FighterStatus::Attack; body.action_frame=0; body.hit_mask=0; body.vel_ground=0;body.attack_epoch=~0U;body.hit_group_epochs.fill(~0U);
    };
    if (body.status==FighterStatus::Attack && body.jab_stage>=4) {
        if (animation_ended) {
            if (body.jab_stage==4) start(5);
            else if (body.jab_stage==5) {start(body.rapid_continue?5:6);body.rapid_continue=false;}
            else {body.status=FighterStatus::Wait;body.jab_stage=0;body.rapid_inputs=0;}
        }
        return;
    }
    if (body.status==FighterStatus::Attack) {
        if (pressed && body.jab_followup_left>0 && next) body.jab_queued=true;
        const unsigned clip=body.jab_stage==3?data.jab3:body.jab_stage==2?data.jab2:data.jab;
        const auto flag=std::find_if(source_jab_followups.begin(),source_jab_followups.end(),
                                   [&](const auto& event){return event.kind==static_cast<unsigned>(body.kind) && event.motion==clip;});
        const unsigned rapid_stage=body.kind==FighterKind::Captain?3:2;
        const int threshold=body.kind==FighterKind::Captain?6:body.kind==FighterKind::Link?5:4;
        if (data.rapid[0] && body.jab_stage==rapid_stage && body.rapid_inputs>=threshold &&
            flag!=source_jab_followups.end() && flag->frame>=0 && body.action_frame>=flag->frame) {
            start(4);body.rapid_continue=false;return;
        }
        if (body.jab_queued && flag!=source_jab_followups.end() && flag->frame>=0 && body.action_frame>=flag->frame) {
            start(next); return;
        }
        if (animation_ended) body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;
    } else if (pressed && body.grounded && (body.status==FighterStatus::Wait || body.status==FighterStatus::Walk || body.status==FighterStatus::Dash || body.status==FighterStatus::Run)) {
        if (!body.jab_followup_left) body.rapid_inputs=0;
        start(body.jab_followup_left>0 && next?next:1); return;
    }
    if (body.jab_followup_left>0) --body.jab_followup_left;
}

std::vector<FighterHit> FighterCombat::resolve(std::span<FighterBody> bodies,std::span<const AttackVolume> attacks) {
    std::vector<FighterHit> hits;
    for (const auto& hit:attacks) {
        if (hit.owner>=bodies.size()) continue;
        auto& attacker=bodies[hit.owner];
        if (attacker.status==FighterStatus::Captured || attacker.status==FighterStatus::KO) continue;
        const unsigned group=std::min(7U,hit.group);
        unsigned projectile_mask=hit.excluded_mask;
        unsigned& mask=hit.projectile?projectile_mask:hit.epoch==~0U?attacker.hit_mask:attacker.hit_group_masks[group];
        if (!hit.projectile && hit.epoch!=~0U && attacker.hit_group_epochs[group]!=hit.epoch) {
            attacker.hit_group_epochs[group]=hit.epoch;mask=0;
        }
        for (unsigned i=0;i<bodies.size();++i) {
            auto& defender=bodies[i];
            if (i==hit.owner || defender.status==FighterStatus::KO || defender.stocks<=0 || defender.invincible || defender.recovery_invulnerable || (mask&(1U<<i))) continue;
            // Capsule around the map collision body. Per-joint hurtboxes
            // remain a separate fidelity task; attacks already follow joints.
            const float y=std::clamp(hit.position.y,defender.position.y+defender.attr.width,
                                    defender.position.y+std::max(defender.attr.height-defender.attr.width,defender.attr.width));
            const float dx=hit.position.x-defender.position.x,dy=hit.position.y-y;
            const float radius=hit.radius+defender.attr.width;
            if (dx*dx+dy*dy>radius*radius) continue;
            if (defender.status==FighterStatus::Captured) continue;
            if (hit.grab) {
                if (attacker.capture_target>=0 || defender.status==FighterStatus::DownBounce || defender.status==FighterStatus::DownWait) continue;
                const bool inhale=attacker.kind==FighterKind::Kirby && attacker.status==FighterStatus::Special && attacker.special_index%3==0 && attacker.special_phase==1;
                const bool dive=attacker.kind==FighterKind::Captain && attacker.status==FighterStatus::Special && attacker.special_index%3==1;
                attacker.capture_target=static_cast<int>(i);attacker.status=dive?FighterStatus::Special:FighterStatus::CatchWait;
                if (dive) {attacker.special_phase=4;attacker.special_motion=fighter_source_data[static_cast<unsigned>(attacker.kind)].special_hit[attacker.special_index];attacker.vel_air={};}
                attacker.action_frame=0;attacker.capture_tics=0;attacker.vel_ground=0;
                defender.captured_dive=dive;defender.captured_throw=false;
                defender.capture_motion=0;defender.capture_next=0;defender.capture_frame_origin=0;
                defender.swallowed=attacker.kind==FighterKind::Yoshi;
                if (inhale) {
                    attacker.status=FighterStatus::Special;attacker.special_phase=4;
                    attacker.special_motion=fighter_source_data[8].special_hit[attacker.special_index];defender.swallowed=true;
                }
                defender.captured_by=static_cast<int>(hit.owner);defender.status=FighterStatus::Captured;
                defender.action_frame=0;defender.vel_air={};defender.vel_damage={};defender.vel_ground=0;
                defender.attack_motion=0;defender.aerial_attack=-1;defender.hitlag=0;
                hits.push_back({hit.owner,i,false,hit.fgm});
                break;
            }
            if (hit.element==6) {
                // Sing only affects grounded, unshielded targets; it does no damage/knockback.
                if (!defender.grounded || defender.status==FighterStatus::Shield || defender.status==FighterStatus::Sleep) continue;
                mask|=1U<<i;defender.status=FighterStatus::Sleep;defender.action_frame=0;
                defender.sleep_tics=std::max(0,300-static_cast<int>(defender.damage))+75;
                defender.vel_air={};defender.vel_damage={};defender.vel_ground=0;defender.hitlag=0;
                defender.attack_motion=0;defender.aerial_buffer=0;defender.smash_buffer=0;
                hits.push_back({hit.owner,i,false,hit.fgm,hit.element});continue;
            }
            mask|=1U<<i;
            defender.swallowed=false;
            const int lag=hit.damage/3+4;
            defender.hitlag=lag;
            if (!hit.projectile) attacker.hitlag=lag;
            const bool shield=defender.status==FighterStatus::Shield;
            hits.push_back({hit.owner,i,shield,hit.fgm,hit.element});
            if (shield) {
                defender.shield=std::max(0.0f,defender.shield-hit.damage);
                defender.shield_stun=static_cast<int>(hit.damage*1.62f+4);
                defender.vel_ground=(attacker.position.x<defender.position.x?1.f:-1.f)*defender.lr*defender.shield_stun*2.f;
                continue; // Shield-break states are intentionally not implemented yet.
            }
            defender.smash_buffer=0;defender.aerial_buffer=0;
            defender.damage+=hit.damage;
            const float p=defender.damage;
            const float component=hit.weight ? 1+hit.weight*.5f : p*.1f+p*hit.damage*.05f;
            const float knockback=std::min(2500.0f,((component*defender.attr.weight*1.4f+18)*hit.growth*.01f)+hit.base);
            const float degrees=hit.angle==361 ? (defender.grounded ? (knockback<32?0.f:std::min(42.5f,((knockback-32)/.099998474f)*42.5f+1)) : 43.f) : static_cast<float>(hit.angle);
            const float angle=degrees*.01745329252f;
            const bool was_airborne=!defender.grounded;
            const int hit_lr=hit.facing?hit.facing:attacker.lr;
            defender.lr=-hit_lr;
            defender.vel_damage={std::cos(angle)*knockback*hit_lr,std::sin(angle)*knockback,0};
            defender.vel_air={}; defender.vel_ground=0;
            if (defender.vel_damage.y>0) defender.grounded=false;
            defender.status=FighterStatus::Hitstun; defender.action_frame=0;
            defender.attack_motion=0;defender.jab_stage=0;defender.jab_followup_left=0;defender.rapid_inputs=0;
            defender.hitstun=std::max(1,static_cast<int>(knockback/1.875f));
            const unsigned level=defender.hitstun<12?0:defender.hitstun<24?1:defender.hitstun<32?2:3;
            // Until per-joint hurtboxes are available, classify contact height
            // on the body capsule into the source low/neutral/high indices.
            const float height=(hit.position.y-defender.position.y)/defender.attr.height;
            const unsigned band=height<.333f?0:height<.667f?1:2;
            unsigned reaction=was_airborne?9+level:(2-band)*3+level;
            if (level==3) {
                reaction=15-band;
                if (degrees>75 && degrees<115) reaction=16;
                if (defender.grounded) {
                    defender.grounded=false;
                    if (defender.vel_damage.y<0) defender.vel_damage.y*=-.8f;
                }
            }
            defender.damage_motion=fighter_source_data[static_cast<unsigned>(defender.kind)].damage_reactions[reaction];
            defender.damage_tumble=was_airborne || level==3;
            defender.shield_tics=255;
            defender.tap_stick_x=defender.tap_stick_y=255;
            defender.fastfall=false;
        }
    }
    return hits;
}

} // namespace sagas
