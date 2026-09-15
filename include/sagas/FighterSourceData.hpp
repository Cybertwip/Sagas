// US cartridge values from FTAttributes and scSubsys motion tables.
#pragma once
#include <array>
#include <sagas/FighterDescriptors.hpp>
namespace sagas {
struct FighterSourceData {
 float size,walk_mul,traction,dash,run,kneebend,jump_x,jump_mul,jump_base,air_accel,air_max,air_friction,gravity,terminal,fast,weight,height,width,select_scale,aerial_x,aerial_height,jab_window,cam_offset_y,camera_zoom,dash_decel,dash_to_run;
 unsigned jumps,idle,walk,dash_clip,run_clip,jump,fall,landing,jab,damage,selected,announce,selected_flags,kneebend_clip,jump_back,aerial_forward,aerial_back,jab2,jab3,run_brake;
 std::array<unsigned,5> multi_jump;
 std::array<unsigned,3> smash,rapid,smash_voices;
 std::array<unsigned,5> attack_air,landing_air;
 std::array<unsigned,4> grab;
 std::array<unsigned,3> crouch;
 std::array<unsigned,7> tilt;
 std::array<unsigned,3> walks;
 std::array<float,3> walk_lengths;
 unsigned landing_sfx,down_sfx,deadup_sfx;
 std::array<unsigned,2> dead_sfx;
 unsigned dash_attack,turn;
 unsigned taunt,capture_joint;
 std::array<unsigned,3> capture;
 std::array<unsigned,4> guard;
 std::array<unsigned,3> tech;
 std::array<unsigned,6> special_start,special_loop,special_end,special_active,special_hit;
 std::array<std::array<unsigned,6>,5> special_events;
 std::array<unsigned,12> down; // Bounce, stand, tech rolls, get-up rolls, attacks (D/U pairs).
 std::array<unsigned,20> damage_reactions; // Common motions DamageHi1 through DamageFall.
 std::array<unsigned,8> cliff;
 std::array<float,2> cliff_box;
};
inline void descriptor_read(std::istream& input,FighterSourceData& value) {
    descriptor_read(input,value.size);
    descriptor_read(input,value.walk_mul);
    descriptor_read(input,value.traction);
    descriptor_read(input,value.dash);
    descriptor_read(input,value.run);
    descriptor_read(input,value.kneebend);
    descriptor_read(input,value.jump_x);
    descriptor_read(input,value.jump_mul);
    descriptor_read(input,value.jump_base);
    descriptor_read(input,value.air_accel);
    descriptor_read(input,value.air_max);
    descriptor_read(input,value.air_friction);
    descriptor_read(input,value.gravity);
    descriptor_read(input,value.terminal);
    descriptor_read(input,value.fast);
    descriptor_read(input,value.weight);
    descriptor_read(input,value.height);
    descriptor_read(input,value.width);
    descriptor_read(input,value.select_scale);
    descriptor_read(input,value.aerial_x);
    descriptor_read(input,value.aerial_height);
    descriptor_read(input,value.jab_window);
    descriptor_read(input,value.cam_offset_y);
    descriptor_read(input,value.camera_zoom);
    descriptor_read(input,value.dash_decel);
    descriptor_read(input,value.dash_to_run);
    descriptor_read(input,value.jumps);
    descriptor_read(input,value.idle);
    descriptor_read(input,value.walk);
    descriptor_read(input,value.dash_clip);
    descriptor_read(input,value.run_clip);
    descriptor_read(input,value.jump);
    descriptor_read(input,value.fall);
    descriptor_read(input,value.landing);
    descriptor_read(input,value.jab);
    descriptor_read(input,value.damage);
    descriptor_read(input,value.selected);
    descriptor_read(input,value.announce);
    descriptor_read(input,value.selected_flags);
    descriptor_read(input,value.kneebend_clip);
    descriptor_read(input,value.jump_back);
    descriptor_read(input,value.aerial_forward);
    descriptor_read(input,value.aerial_back);
    descriptor_read(input,value.jab2);
    descriptor_read(input,value.jab3);
    descriptor_read(input,value.run_brake);
    descriptor_read(input,value.multi_jump);
    descriptor_read(input,value.smash);
    descriptor_read(input,value.rapid);
    descriptor_read(input,value.smash_voices);
    descriptor_read(input,value.attack_air);
    descriptor_read(input,value.landing_air);
    descriptor_read(input,value.grab);
    descriptor_read(input,value.crouch);
    descriptor_read(input,value.tilt);
    descriptor_read(input,value.walks);
    descriptor_read(input,value.walk_lengths);
    descriptor_read(input,value.landing_sfx);
    descriptor_read(input,value.down_sfx);
    descriptor_read(input,value.deadup_sfx);
    descriptor_read(input,value.dead_sfx);
    descriptor_read(input,value.dash_attack);
    descriptor_read(input,value.turn);
    descriptor_read(input,value.taunt);
    descriptor_read(input,value.capture_joint);
    descriptor_read(input,value.capture);
    descriptor_read(input,value.guard);
    descriptor_read(input,value.tech);
    descriptor_read(input,value.special_start);
    descriptor_read(input,value.special_loop);
    descriptor_read(input,value.special_end);
    descriptor_read(input,value.special_active);
    descriptor_read(input,value.special_hit);
    descriptor_read(input,value.special_events);
    descriptor_read(input,value.down);
    descriptor_read(input,value.damage_reactions);
    descriptor_read(input,value.cliff);
    descriptor_read(input,value.cliff_box);
}

inline const DescriptorTable<FighterSourceData> fighter_source_data{"fighter_source_data.tsv","size\twalk_mul\ttraction\tdash\trun\tkneebend\tjump_x\tjump_mul\tjump_base\tair_accel\tair_max\tair_friction\tgravity\tterminal\tfast\tweight\theight\twidth\tselect_scale\taerial_x\taerial_height\tjab_window\tcam_offset_y\tcamera_zoom\tdash_decel\tdash_to_run\tjumps\tidle\twalk\tdash_clip\trun_clip\tjump\tfall\tlanding\tjab\tdamage\tselected\tannounce\tselected_flags\tkneebend_clip\tjump_back\taerial_forward\taerial_back\tjab2\tjab3\trun_brake\tmulti_jump[0]\tmulti_jump[1]\tmulti_jump[2]\tmulti_jump[3]\tmulti_jump[4]\tsmash[0]\tsmash[1]\tsmash[2]\trapid[0]\trapid[1]\trapid[2]\tsmash_voices[0]\tsmash_voices[1]\tsmash_voices[2]\tattack_air[0]\tattack_air[1]\tattack_air[2]\tattack_air[3]\tattack_air[4]\tlanding_air[0]\tlanding_air[1]\tlanding_air[2]\tlanding_air[3]\tlanding_air[4]\tgrab[0]\tgrab[1]\tgrab[2]\tgrab[3]\tcrouch[0]\tcrouch[1]\tcrouch[2]\ttilt[0]\ttilt[1]\ttilt[2]\ttilt[3]\ttilt[4]\ttilt[5]\ttilt[6]\twalks[0]\twalks[1]\twalks[2]\twalk_lengths[0]\twalk_lengths[1]\twalk_lengths[2]\tlanding_sfx\tdown_sfx\tdeadup_sfx\tdead_sfx[0]\tdead_sfx[1]\tdash_attack\tturn\ttaunt\tcapture_joint\tcapture[0]\tcapture[1]\tcapture[2]\tguard[0]\tguard[1]\tguard[2]\tguard[3]\ttech[0]\ttech[1]\ttech[2]\tspecial_start[0]\tspecial_start[1]\tspecial_start[2]\tspecial_start[3]\tspecial_start[4]\tspecial_start[5]\tspecial_loop[0]\tspecial_loop[1]\tspecial_loop[2]\tspecial_loop[3]\tspecial_loop[4]\tspecial_loop[5]\tspecial_end[0]\tspecial_end[1]\tspecial_end[2]\tspecial_end[3]\tspecial_end[4]\tspecial_end[5]\tspecial_active[0]\tspecial_active[1]\tspecial_active[2]\tspecial_active[3]\tspecial_active[4]\tspecial_active[5]\tspecial_hit[0]\tspecial_hit[1]\tspecial_hit[2]\tspecial_hit[3]\tspecial_hit[4]\tspecial_hit[5]\tspecial_events[0][0]\tspecial_events[0][1]\tspecial_events[0][2]\tspecial_events[0][3]\tspecial_events[0][4]\tspecial_events[0][5]\tspecial_events[1][0]\tspecial_events[1][1]\tspecial_events[1][2]\tspecial_events[1][3]\tspecial_events[1][4]\tspecial_events[1][5]\tspecial_events[2][0]\tspecial_events[2][1]\tspecial_events[2][2]\tspecial_events[2][3]\tspecial_events[2][4]\tspecial_events[2][5]\tspecial_events[3][0]\tspecial_events[3][1]\tspecial_events[3][2]\tspecial_events[3][3]\tspecial_events[3][4]\tspecial_events[3][5]\tspecial_events[4][0]\tspecial_events[4][1]\tspecial_events[4][2]\tspecial_events[4][3]\tspecial_events[4][4]\tspecial_events[4][5]\tdown[0]\tdown[1]\tdown[2]\tdown[3]\tdown[4]\tdown[5]\tdown[6]\tdown[7]\tdown[8]\tdown[9]\tdown[10]\tdown[11]\tdamage_reactions[0]\tdamage_reactions[1]\tdamage_reactions[2]\tdamage_reactions[3]\tdamage_reactions[4]\tdamage_reactions[5]\tdamage_reactions[6]\tdamage_reactions[7]\tdamage_reactions[8]\tdamage_reactions[9]\tdamage_reactions[10]\tdamage_reactions[11]\tdamage_reactions[12]\tdamage_reactions[13]\tdamage_reactions[14]\tdamage_reactions[15]\tdamage_reactions[16]\tdamage_reactions[17]\tdamage_reactions[18]\tdamage_reactions[19]\tcliff[0]\tcliff[1]\tcliff[2]\tcliff[3]\tcliff[4]\tcliff[5]\tcliff[6]\tcliff[7]\tcliff_box[0]\tcliff_box[1]",12};
struct SourceAnimationFlags { unsigned motion,flags; };
inline void descriptor_read(std::istream& input,SourceAnimationFlags& value) {
    descriptor_read(input,value.motion);
    descriptor_read(input,value.flags);
}

inline const DescriptorTable<SourceAnimationFlags> source_animation_flags{"source_animation_flags.tsv","motion\tflags",1656};
inline unsigned fighter_motion_flags(unsigned motion) { for (const auto& entry:source_animation_flags) if (entry.motion==motion) return entry.flags; return 0; }
struct VictoryMotion { unsigned win,clap; };
inline void descriptor_read(std::istream& input,VictoryMotion& value) {
    descriptor_read(input,value.win);descriptor_read(input,value.clap);
}
inline const DescriptorTable<VictoryMotion> victory_motions{"victory_motions.tsv","win\tclap",12};
}
namespace sagas { inline const DescriptorValue<unsigned> guard_on_sfx{"guard_on_sfx.tsv"};
inline const DescriptorValue<unsigned> guard_off_sfx{"guard_off_sfx.tsv"};
inline const DescriptorValue<unsigned> dead_explode_sfx{"dead_explode_sfx.tsv"};
inline const DescriptorValue<unsigned> dead_star_sfx{"dead_star_sfx.tsv"}; }
