#include <sagas/Fighter.hpp>
#include <sagas/FighterSourceData.hpp>
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
    body.jumps_used=body.grounded ? 1 : body.jumps_used+1;
    body.grounded=false; body.fastfall=false;
    body.vel_air.y=body.attr.jump_vel_y*(body.jumps_used>1?.9f:1.0f);
    body.vel_air.x=body.stick_x*body.attr.jump_vel_x;
    body.vel_ground=0; body.status=FighterStatus::Jump; body.jump_frames=0;
}

void FighterPhysics::tick(FighterBody& body,float ground_y) noexcept {
    const CollisionSegment floor{{-1000000,ground_y},{1000000,ground_y},0,0,false};
    tick(body,std::span<const CollisionSegment>(&floor,1));
}

void FighterPhysics::tick(FighterBody& body,std::span<const CollisionSegment> stage) noexcept {
    if (body.status==FighterStatus::KO) return;
    if (body.invincible>0) --body.invincible;
    if (body.hitlag>0) { --body.hitlag; return; }
    if (body.drop_frames>0) --body.drop_frames;
    const Vec3 before=body.position;
    const bool was_grounded=body.grounded;
    if (body.jump_pressed && body.status!=FighterStatus::Hitstun) {
        if (body.grounded && body.status!=FighterStatus::KneeBend) {
            body.status=FighterStatus::KneeBend; body.jump_frames=0;
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
    if (body.status==FighterStatus::KneeBend && ++body.jump_frames>=body.attr.knee_bend) jump(body);
    if (body.grounded) {
        if (body.status==FighterStatus::Land && --body.land_frames<=0) body.status=FighterStatus::Wait;
        const bool locked=body.status==FighterStatus::KneeBend || body.status==FighterStatus::Attack ||
                          body.status==FighterStatus::Shield || body.status==FighterStatus::Hitstun || body.status==FighterStatus::Land;
        if (!locked && std::abs(body.stick_x)>=kStickMin) {
            body.lr=body.stick_x>0?1:-1;
            body.vel_ground=std::abs(body.stick_x)>=56 ? body.attr.run_speed :
                body.attr.walk_speed*std::abs(body.stick_x)/80.0f;
            body.status=std::abs(body.stick_x)>=56?FighterStatus::Dash:FighterStatus::Walk;
        } else {
            apply_ground_friction(body);
            if (!locked) body.status=FighterStatus::Wait;
        }
        body.vel_air.x=body.vel_ground*body.lr; body.vel_air.y=0;
    } else {
        if (body.stick_y<-44 && body.vel_air.y<0) body.fastfall=true;
        if (body.status==FighterStatus::Hitstun) apply_gravity_clamp_tvel(body,body.attr.gravity,body.attr.tvel_base);
        else apply_air_vel_drift(body);
        if (body.vel_air.y<0 && body.status==FighterStatus::Jump) body.status=FighterStatus::Fall;
    }
    body.position.x+=body.vel_air.x; body.position.y+=body.vel_air.y;
    body.grounded=false;
    float floor=-std::numeric_limits<float>::infinity();
    for (const auto& line:stage) {
        if (line.type==0 && std::abs(line.b.x-line.a.x)>.001f) {
            if (line.pass_through && body.drop_frames) continue;
            const float x=body.position.x;
            if (x<std::min(line.a.x,line.b.x) || x>std::max(line.a.x,line.b.x)) continue;
            const float y=line.a.y+(line.b.y-line.a.y)*(x-line.a.x)/(line.b.x-line.a.x);
            const float old_y=line.a.y+(line.b.y-line.a.y)*(before.x-line.a.x)/(line.b.x-line.a.x);
            if (body.vel_air.y<=0 && before.y>=old_y-2 &&
                (body.position.y<=y || (was_grounded && std::abs(y-body.position.y)<100))) floor=std::max(floor,y);
        } else if (line.type==1 && std::abs(line.b.x-line.a.x)>.001f) {
            const float x=body.position.x;
            if (x<std::min(line.a.x,line.b.x) || x>std::max(line.a.x,line.b.x)) continue;
            const float y=line.a.y+(line.b.y-line.a.y)*(x-line.a.x)/(line.b.x-line.a.x);
            if (body.vel_air.y>0 && before.y+body.attr.height<=y && body.position.y+body.attr.height>=y) {
                body.position.y=y-body.attr.height; body.vel_air.y=0;
            }
        } else if (line.type>=2 && std::abs(line.b.y-line.a.y)>.001f) {
            if (body.position.y+body.attr.height<std::min(line.a.y,line.b.y) || body.position.y>std::max(line.a.y,line.b.y)) continue;
            const float x=line.a.x+(line.b.x-line.a.x)*(body.position.y-line.a.y)/(line.b.y-line.a.y);
            const float side=body.vel_air.x>=0?body.attr.width:-body.attr.width;
            if ((before.x+side-x)*(body.position.x+side-x)<=0) {
                body.position.x=x-side; body.vel_air.x=body.vel_ground=0;
            }
        }
    }
    if (std::isfinite(floor)) {
        body.position.y=floor; body.vel_air.y=0; body.grounded=true; body.fastfall=false; body.jumps_used=0;
        if (!was_grounded && body.status!=FighterStatus::Attack && body.status!=FighterStatus::Hitstun) {
            body.status=FighterStatus::Land; body.land_frames=4;
        }
    } else if (body.status==FighterStatus::Wait || body.status==FighterStatus::Walk || body.status==FighterStatus::Dash)
        body.status=FighterStatus::Fall;
    body.jump_pressed=false;
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
            hits.push_back({hit.owner,i,shield});
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
            defender.vel_air={std::cos(angle)*knockback*attacker.lr,std::sin(angle)*knockback,0};
            defender.vel_ground=0; defender.grounded=false;
            defender.position.y+=1;
            defender.status=FighterStatus::Hitstun; defender.action_frame=0;
            defender.hitstun=std::max(1,static_cast<int>(knockback/1.875f));
            defender.fastfall=false;
        }
    }
    return hits;
}

} // namespace sagas
