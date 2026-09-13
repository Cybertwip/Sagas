// Generated from US jab scripts by tools/extract_battle_attacks.py.
#pragma once
#include <array>
#include <sagas/FighterDescriptors.hpp>
namespace sagas {
struct SourceHitbox { unsigned motion,begin,end,id,joint; int damage,radius,x,y,z,angle,growth,weight,base; unsigned fgm,epoch,group,element,kind; };
inline void descriptor_read(std::istream& input,SourceHitbox& value) {
    descriptor_read(input,value.motion);
    descriptor_read(input,value.begin);
    descriptor_read(input,value.end);
    descriptor_read(input,value.id);
    descriptor_read(input,value.joint);
    descriptor_read(input,value.damage);
    descriptor_read(input,value.radius);
    descriptor_read(input,value.x);
    descriptor_read(input,value.y);
    descriptor_read(input,value.z);
    descriptor_read(input,value.angle);
    descriptor_read(input,value.growth);
    descriptor_read(input,value.weight);
    descriptor_read(input,value.base);
    descriptor_read(input,value.fgm);
    descriptor_read(input,value.epoch);
    descriptor_read(input,value.group);
    descriptor_read(input,value.element);
    descriptor_read(input,value.kind);
}

inline const DescriptorTable<SourceHitbox> source_jab_hitboxes{"source_jab_hitboxes.tsv","motion\tbegin\tend\tid\tjoint\tdamage\tradius\tx\ty\tz\tangle\tgrowth\tweight\tbase\tfgm\tepoch\tgroup\telement\tkind",1725};
struct SourceJabFollowup { unsigned motion; int frame; unsigned kind; };
inline void descriptor_read(std::istream& input,SourceJabFollowup& value) {
    descriptor_read(input,value.motion);
    descriptor_read(input,value.frame);
    descriptor_read(input,value.kind);
}

inline const DescriptorTable<SourceJabFollowup> source_jab_followups{"source_jab_followups.tsv","motion\tframe\tkind",877};
struct SourceMotionFlag { unsigned motion,frame; int value; unsigned kind; };
inline void descriptor_read(std::istream& input,SourceMotionFlag& value) {
    descriptor_read(input,value.motion);
    descriptor_read(input,value.frame);
    descriptor_read(input,value.value);
    descriptor_read(input,value.kind);
}

inline const DescriptorTable<SourceMotionFlag> source_motion_flags{"source_motion_flags.tsv","motion\tframe\tvalue\tkind",358};
struct SourceHitStatus { unsigned motion,frame,status,kind; };
inline void descriptor_read(std::istream& input,SourceHitStatus& value) {
    descriptor_read(input,value.motion);
    descriptor_read(input,value.frame);
    descriptor_read(input,value.status);
    descriptor_read(input,value.kind);
}

inline const DescriptorTable<SourceHitStatus> source_hit_status{"source_hit_status.tsv","motion\tframe\tstatus\tkind",405};
struct SourceSpecialFlag { unsigned motion,frame,flag,value,kind; };
inline void descriptor_read(std::istream& input,SourceSpecialFlag& value) {
    descriptor_read(input,value.motion);
    descriptor_read(input,value.frame);
    descriptor_read(input,value.flag);
    descriptor_read(input,value.value);
    descriptor_read(input,value.kind);
}

inline const DescriptorTable<SourceSpecialFlag> source_special_flags{"source_special_flags.tsv","motion\tframe\tflag\tvalue\tkind",307};
struct SourceModelPart { unsigned motion,frame; int joint,part; unsigned kind; };
inline void descriptor_read(std::istream& input,SourceModelPart& value) {
    descriptor_read(input,value.motion);
    descriptor_read(input,value.frame);
    descriptor_read(input,value.joint);
    descriptor_read(input,value.part);
    descriptor_read(input,value.kind);
}

inline const DescriptorTable<SourceModelPart> source_model_parts{"source_model_parts.tsv","motion\tframe\tjoint\tpart\tkind",316};
}
