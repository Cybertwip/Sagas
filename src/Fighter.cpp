#include <sagas/Fighter.hpp>

#include <algorithm>
#include <cmath>

namespace sagas {
namespace {

void set_grounded(FighterBody& body, float ground_y) {
    const float feet = body.position.y;
    if (body.vel_air.y <= 0.0f && feet <= ground_y) {
        body.position.y = ground_y;
        body.vel_air.y = 0.0f;
        body.vel_air.z = 0.0f;
        if (!body.grounded) body.land_frames = 4;
        body.grounded = true;
        body.fastfall = false;
        body.jump_frames = 0;
        if (body.status == FighterStatus::Jump || body.status == FighterStatus::Fall)
            body.status = body.land_frames ? FighterStatus::Land : FighterStatus::Wait;
    } else if (feet > ground_y + 0.01f) {
        body.grounded = false;
        if (body.status == FighterStatus::Wait || body.status == FighterStatus::Walk ||
            body.status == FighterStatus::Dash || body.status == FighterStatus::Land)
            body.status = FighterStatus::Fall;
    }
}

} // namespace

FighterAttributes fighter_attributes(FighterKind kind) {
    FighterAttributes attr;
    switch (kind) {
        case FighterKind::Donkey: attr.gravity=0.095f; attr.tvel_base=1.92f; attr.walk_speed=1.2f; attr.dash_speed=1.8f; attr.height=26; attr.width=10; break;
        case FighterKind::Fox: attr.gravity=0.13f; attr.tvel_base=2.05f; attr.tvel_fast=2.8f; attr.jump_vel_y=3.42f; attr.walk_speed=1.5f; attr.dash_speed=2.2f; attr.air_speed_max_x=0.83f; attr.height=16; break;
        case FighterKind::Kirby: attr.gravity=0.08f; attr.tvel_base=1.33f; attr.jump_vel_y=2.5f; attr.walk_speed=1.1f; attr.height=12; attr.width=8; break;
        case FighterKind::Pikachu: attr.gravity=0.11f; attr.tvel_base=1.62f; attr.jump_vel_y=3.0f; attr.walk_speed=1.24f; attr.dash_speed=1.8f; attr.height=14; break;
        case FighterKind::Captain: attr.gravity=0.11f; attr.jump_vel_y=3.1f; attr.walk_speed=1.1f; attr.dash_speed=1.9f; attr.height=22; break;
        case FighterKind::Samus: attr.gravity=0.066f; attr.tvel_base=1.4f; attr.jump_vel_y=2.5f; attr.walk_speed=1.0f; attr.height=22; break;
        default: break;
    }
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
    if (!body.grounded && body.status != FighterStatus::KneeBend) return;
    body.grounded = false;
    body.fastfall = false;
    body.vel_air.y = body.attr.jump_vel_y;
    body.vel_air.x = body.vel_ground * body.lr * 0.25f + body.stick_x / 80.0f * 0.4f;
    body.vel_ground = 0;
    body.status = FighterStatus::Jump;
    body.jump_frames = 1;
}

void FighterPhysics::tick(FighterBody& body, float ground_y) noexcept {
    if (body.tap_stick_y < 255) ++body.tap_stick_y;
    if (body.stick_y <= -44 && body.vel_air.y < 0.0f && !body.grounded) body.fastfall = true;

    if (body.grounded && body.status == FighterStatus::KneeBend) {
        if (++body.jump_frames >= 3) jump(body);
    }
    if (body.grounded) {
        if (body.status == FighterStatus::Land) {
            if (--body.land_frames <= 0) body.status = FighterStatus::Wait;
            apply_ground_friction(body);
        } else if (std::abs(body.stick_x) >= kStickMin) {
            body.lr = body.stick_x >= 0 ? 1 : -1;
            const float target = (std::abs(body.stick_x) >= 40 ? body.attr.dash_speed : body.attr.walk_speed) *
                                 (std::abs(body.stick_x) / 80.0f);
            if (body.vel_ground < target) body.vel_ground = target;
            else apply_ground_friction(body);
            clamp_ground_vel(body, body.attr.dash_speed);
            body.status = std::abs(body.stick_x) >= 40 ? FighterStatus::Dash : FighterStatus::Walk;
            body.vel_air.x = body.lr * body.vel_ground;
        } else {
            apply_ground_friction(body);
            body.vel_air.x = body.lr * body.vel_ground;
            body.status = FighterStatus::Wait;
        }
        body.vel_air.y = 0;
    } else {
        apply_air_vel_drift(body);
        if (body.vel_air.y < 0.0f) body.status = FighterStatus::Fall;
    }

    body.position.x += body.vel_air.x;
    body.position.y += body.vel_air.y;
    body.position.z += body.vel_air.z;
    set_grounded(body, ground_y);
}

} // namespace sagas
