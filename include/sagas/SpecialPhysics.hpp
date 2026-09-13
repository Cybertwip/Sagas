#pragma once
#include <sagas/FighterDescriptors.hpp>
namespace sagas {
struct SpecialPhysics {
    float donkey_hi_ground_accel;
    float donkey_hi_ground_max;
    float donkey_hi_air_accel;
    float donkey_hi_air_max;
    float donkey_hi_gravity;
    float donkey_hi_launch;
    float link_hi_gravity;
    float link_hi_launch;
    float samus_hi_launch;
    float samus_hi_accel;
    float samus_hi_max;
    float samus_lw_air_launch;
    float samus_lw_drift;
    float ness_hi_hold_gravity;
    float yoshi_lw_speed;
    float yoshi_lw_drift_max;
    float yoshi_star_speed;
    float yoshi_star_angle;
    float yoshi_star_offset_x;
    float yoshi_star_offset_y;
    float yoshi_star_life;
    float yoshi_star_decel;
    float link_bomb_throw_x;
    float link_bomb_throw_y;
    float link_bomb_vertical_drift;
    float link_bomb_throw_up;
    float link_bomb_throw_down;
    float samus_tether_start;
    float samus_tether_end;
};
inline void descriptor_read(std::istream& input, SpecialPhysics& value) {
    descriptor_read(input,value.donkey_hi_ground_accel);
    descriptor_read(input,value.donkey_hi_ground_max);
    descriptor_read(input,value.donkey_hi_air_accel);
    descriptor_read(input,value.donkey_hi_air_max);
    descriptor_read(input,value.donkey_hi_gravity);
    descriptor_read(input,value.donkey_hi_launch);
    descriptor_read(input,value.link_hi_gravity);
    descriptor_read(input,value.link_hi_launch);
    descriptor_read(input,value.samus_hi_launch);
    descriptor_read(input,value.samus_hi_accel);
    descriptor_read(input,value.samus_hi_max);
    descriptor_read(input,value.samus_lw_air_launch);
    descriptor_read(input,value.samus_lw_drift);
    descriptor_read(input,value.ness_hi_hold_gravity);
    descriptor_read(input,value.yoshi_lw_speed);
    descriptor_read(input,value.yoshi_lw_drift_max);
    descriptor_read(input,value.yoshi_star_speed);
    descriptor_read(input,value.yoshi_star_angle);
    descriptor_read(input,value.yoshi_star_offset_x);
    descriptor_read(input,value.yoshi_star_offset_y);
    descriptor_read(input,value.yoshi_star_life);
    descriptor_read(input,value.yoshi_star_decel);
    descriptor_read(input,value.link_bomb_throw_x);
    descriptor_read(input,value.link_bomb_throw_y);
    descriptor_read(input,value.link_bomb_vertical_drift);
    descriptor_read(input,value.link_bomb_throw_up);
    descriptor_read(input,value.link_bomb_throw_down);
    descriptor_read(input,value.samus_tether_start);
    descriptor_read(input,value.samus_tether_end);
}
inline const DescriptorTable<SpecialPhysics> special_physics{"special_physics.tsv","donkey_hi_ground_accel\tdonkey_hi_ground_max\tdonkey_hi_air_accel\tdonkey_hi_air_max\tdonkey_hi_gravity\tdonkey_hi_launch\tlink_hi_gravity\tlink_hi_launch\tsamus_hi_launch\tsamus_hi_accel\tsamus_hi_max\tsamus_lw_air_launch\tsamus_lw_drift\tness_hi_hold_gravity\tyoshi_lw_speed\tyoshi_lw_drift_max\tyoshi_star_speed\tyoshi_star_angle\tyoshi_star_offset_x\tyoshi_star_offset_y\tyoshi_star_life\tyoshi_star_decel\tlink_bomb_throw_x\tlink_bomb_throw_y\tlink_bomb_vertical_drift\tlink_bomb_throw_up\tlink_bomb_throw_down\tsamus_tether_start\tsamus_tether_end",1};
}
