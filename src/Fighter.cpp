#include <sagas/Fighter.hpp>
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
        body.vel_ground += body.attr.traction;
        if (body.vel_ground > 0.0f) body.vel_ground = 0.0f;
    } else {
        body.vel_ground -= body.attr.traction;
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
    if (body.status==FighterStatus::KO) return;
    if (body.invincible>0) --body.invincible;
    if (body.hitlag>0) { --body.hitlag; return; }
    if (body.cliff_cooldown>0) --body.cliff_cooldown;
    if (body.drop_frames>0) --body.drop_frames;
    const Vec3 before=body.position;
    const bool was_grounded=body.grounded;
    if (body.jump_pressed && body.status!=FighterStatus::Hitstun && body.status!=FighterStatus::Attack) {
        if (body.grounded && body.status!=FighterStatus::KneeBend) {
            body.status=FighterStatus::KneeBend; body.jump_frames=0;
            body.short_hop=false; body.jump_force=body.stick_y;
        } else if (!body.grounded) jump(body);
    }
    if (body.grounded && body.stick_y<-44) {
        for (const auto& line:stage) if (line.type==0 && line.pass_through &&
            body.position.x>=std::min(line.a.x,line.b.x) && body.position.x<=std::max(line.a.x,line.b.x)) {
            const float y=line.a.y+(line.b.y-line.a.y)*(body.position.x-line.a.x)/(line.b.x-line.a.x);
            if (std::abs(body.position.y-y)<2) {
                body.grounded=false; body.drop_frames=12; body.position.y-=3; body.vel_air.y=-3;
                break;
            }
        }
    }
    if (body.status==FighterStatus::KneeBend) {
        ++body.jump_frames;
        if (body.jump_button && body.jump_released && body.jump_frames<=3) body.short_hop=true;
        body.jump_force=std::max(body.jump_force,body.stick_y);
        if (body.jump_frames>=body.attr.knee_bend) jump(body);
    }
    if (body.grounded) {
        if (body.status==FighterStatus::Land && --body.land_frames<=0) body.status=FighterStatus::Wait;
        const bool locked=body.status==FighterStatus::KneeBend || body.status==FighterStatus::Attack ||
                          body.status==FighterStatus::Shield || body.status==FighterStatus::Hitstun || body.status==FighterStatus::Land;
        if (!locked && body.status==FighterStatus::Run && body.stick_x*body.lr<44) body.status=FighterStatus::RunBrake;
        if (!locked && body.status==FighterStatus::RunBrake) {
            body.vel_ground=std::max(0.f,body.vel_ground-body.attr.traction*1.25f);
            if (body.vel_ground==0) body.status=FighterStatus::Wait;
        } else if (!locked && std::abs(body.stick_x)>=kStickMin) {
            body.lr=body.stick_x>0?1:-1;
            body.vel_ground=std::abs(body.stick_x)>=56 ? body.attr.run_speed :
                body.attr.walk_speed*std::abs(body.stick_x)/80.0f;
            body.status=std::abs(body.stick_x)>=56?FighterStatus::Run:FighterStatus::Walk;
        } else {
            apply_ground_friction(body);
            if (!locked) body.status=FighterStatus::Wait;
        }
        body.vel_air.x=body.vel_ground*body.lr; body.vel_air.y=0;
    } else {
        const bool authored=body.aerial_jump && body.status==FighterStatus::Jump &&
                            (body.kind==FighterKind::Ness || body.kind==FighterKind::Yoshi) && motion;
        const auto root_velocity=authored?motion(body):std::optional<Vec3>{};
        if (!authored && body.stick_y<-44 && body.vel_air.y<0) body.fastfall=true;
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
        if (body.status==FighterStatus::Jump) ++body.jump_frames;
    }
    // ftMainProcPhysicsMap keeps knockback separate from controlled velocity.
    // Air knockback decays by 1.7 per tick; grounded knockback uses traction.
    if (body.grounded) {
        const float speed=std::max(0.0f,std::abs(body.vel_damage.x)-body.attr.traction*.25f);
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
    body.position.x+=velocity_x; body.position.y+=velocity_y;
    body.grounded=false;
    float floor=-std::numeric_limits<float>::infinity();
    for (const auto& line:stage) {
        if (line.type==0 && std::abs(line.b.x-line.a.x)>.001f) {
            if (line.pass_through && body.drop_frames) continue;
            const float x=body.position.x;
            if (x<std::min(line.a.x,line.b.x) || x>std::max(line.a.x,line.b.x)) continue;
            const float y=line.a.y+(line.b.y-line.a.y)*(x-line.a.x)/(line.b.x-line.a.x);
            const float old_y=line.a.y+(line.b.y-line.a.y)*(before.x-line.a.x)/(line.b.x-line.a.x);
            if (velocity_y<=0 && before.y>=old_y-2 &&
                (body.position.y<=y || (was_grounded && std::abs(y-body.position.y)<100))) floor=std::max(floor,y);
        } else if (line.type==1 && std::abs(line.b.x-line.a.x)>.001f) {
            const float x=body.position.x;
            if (x<std::min(line.a.x,line.b.x) || x>std::max(line.a.x,line.b.x)) continue;
            const float y=line.a.y+(line.b.y-line.a.y)*(x-line.a.x)/(line.b.x-line.a.x);
            if (velocity_y>0 && before.y+body.attr.height<=y && body.position.y+body.attr.height>=y) {
                body.position.y=y-body.attr.height; body.vel_air.y=body.vel_damage.y=0;
            }
        } else if (line.type>=2 && std::abs(line.b.y-line.a.y)>.001f) {
            if (body.position.y+body.attr.height<std::min(line.a.y,line.b.y) || body.position.y>std::max(line.a.y,line.b.y)) continue;
            const float x=line.a.x+(line.b.x-line.a.x)*(body.position.y-line.a.y)/(line.b.y-line.a.y);
            const float side=velocity_x>=0?body.attr.width:-body.attr.width;
            if ((before.x+side-x)*(body.position.x+side-x)<=0) {
                body.position.x=x-side; body.vel_air.x=body.vel_ground=body.vel_damage.x=0;
            }
        }
    }
    if (std::isfinite(floor)) {
        body.position.y=floor; body.vel_air.y=body.vel_damage.y=0; body.grounded=true; body.fastfall=false; body.jumps_used=0;
        if (!was_grounded && body.status!=FighterStatus::Attack && body.status!=FighterStatus::Hitstun) {
            body.status=FighterStatus::Land; body.land_frames=4;
        }
    } else if (body.status==FighterStatus::Wait || body.status==FighterStatus::Walk || body.status==FighterStatus::Dash || body.status==FighterStatus::Run || body.status==FighterStatus::RunBrake)
        body.status=FighterStatus::Fall;
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

bool FighterCombat::start_smash(FighterBody& body,bool pressed) {
    if (!pressed || body.hitlag || !body.grounded || body.status==FighterStatus::Attack || body.status==FighterStatus::Hitstun) return false;
    int direction=-1;
    if (body.tap_stick_y<4 && body.stick_y>=53) direction=1;
    else if (body.tap_stick_y<4 && body.stick_y<=-53) direction=2;
    else if (body.tap_stick_x<3 && std::abs(body.stick_x)>=56) {direction=0;body.lr=body.stick_x>0?1:-1;}
    if (direction<0) return false;
    body.attack_motion=fighter_source_data[static_cast<unsigned>(body.kind)].smash[direction];
    body.status=FighterStatus::Attack;body.action_frame=0;body.hit_mask=0;body.attack_epoch=~0U;
    body.jab_stage=0;body.jab_followup_left=0;body.vel_ground=0;
    return true;
}

void FighterCombat::advance_jab(FighterBody& body,bool pressed,bool animation_ended,bool released) {
    if (body.hitlag) return;
    const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
    if (body.attack_motion) {
        if (animation_ended) {body.attack_motion=0;body.status=body.grounded?FighterStatus::Wait:FighterStatus::Fall;}
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
        body.status=FighterStatus::Attack; body.action_frame=0; body.hit_mask=0; body.vel_ground=0;body.attack_epoch=~0U;
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
                                   [&](const auto& event){return event.motion==clip;});
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
    } else if (pressed && body.grounded && body.status!=FighterStatus::Hitstun) {
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
        for (unsigned i=0;i<bodies.size();++i) {
            auto& defender=bodies[i];
            if (i==hit.owner || defender.stocks<=0 || defender.invincible || (attacker.hit_mask&(1U<<i))) continue;
            // Capsule around the map collision body. Per-joint hurtboxes
            // remain a separate fidelity task; attacks already follow joints.
            const float y=std::clamp(hit.position.y,defender.position.y+defender.attr.width,
                                    defender.position.y+std::max(defender.attr.height-defender.attr.width,defender.attr.width));
            const float dx=hit.position.x-defender.position.x,dy=hit.position.y-y;
            const float radius=hit.radius+defender.attr.width;
            if (dx*dx+dy*dy>radius*radius) continue;
            attacker.hit_mask|=1U<<i;
            const int lag=hit.damage/3+4;
            attacker.hitlag=defender.hitlag=lag;
            const bool shield=defender.status==FighterStatus::Shield;
            hits.push_back({hit.owner,i,shield,hit.fgm});
            if (shield) {
                defender.shield=std::max(0.0f,defender.shield-hit.damage);
                if (defender.shield>0) continue;
            }
            defender.damage+=hit.damage;
            const float p=defender.damage;
            const float component=hit.weight ? 1+hit.weight*.5f : p*.1f+p*hit.damage*.05f;
            const float knockback=std::min(2500.0f,((component*defender.attr.weight*1.4f+18)*hit.growth*.01f)+hit.base);
            const float degrees=hit.angle==361 ? (defender.grounded && knockback<32 ? 0.0f:45.0f) : static_cast<float>(hit.angle);
            const float angle=degrees*.01745329252f;
            defender.vel_damage={std::cos(angle)*knockback*attacker.lr,std::sin(angle)*knockback,0};
            defender.vel_air={}; defender.vel_ground=0;
            if (defender.vel_damage.y>0) defender.grounded=false;
            defender.status=FighterStatus::Hitstun; defender.action_frame=0;
            defender.attack_motion=0;defender.jab_stage=0;defender.jab_followup_left=0;defender.rapid_inputs=0;
            defender.hitstun=std::max(1,static_cast<int>(knockback/1.875f));
            defender.fastfall=false;
        }
    }
    return hits;
}

} // namespace sagas
