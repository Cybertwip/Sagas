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
}
inline const DescriptorTable<SpecialPhysics> special_physics{"special_physics.tsv","donkey_hi_ground_accel\tdonkey_hi_ground_max\tdonkey_hi_air_accel\tdonkey_hi_air_max\tdonkey_hi_gravity\tdonkey_hi_launch\tlink_hi_gravity\tlink_hi_launch\tsamus_hi_launch\tsamus_hi_accel\tsamus_hi_max\tsamus_lw_air_launch\tsamus_lw_drift\tness_hi_hold_gravity",1};
}
