#pragma once
#include <sagas/Fighter.hpp>
#include <sagas/FighterDescriptors.hpp>
#include <sagas/FighterSourceData.hpp>
#include <cstdint>
namespace sagas {
struct RemixFighter {
    unsigned id;
    std::string key;
    unsigned parent;
    unsigned attribute_offset;
    unsigned extra_actions;
    unsigned jab3;
    unsigned copy;
    std::array<unsigned,9> files;
};
inline void descriptor_read(std::istream& input,RemixFighter& value) {
    descriptor_read(input,value.id);
    descriptor_read(input,value.key);
    descriptor_read(input,value.parent);
    descriptor_read(input,value.attribute_offset);
    descriptor_read(input,value.extra_actions);
    descriptor_read(input,value.jab3);
    descriptor_read(input,value.copy);
    descriptor_read(input,value.files);
}
inline const DescriptorTable<RemixFighter> remix_roster{"remix_roster.tsv","id\tkey\tparent\tattribute_offset\textra_actions\tjab3\tcopy\tfiles[0]\tfiles[1]\tfiles[2]\tfiles[3]\tfiles[4]\tfiles[5]\tfiles[6]\tfiles[7]\tfiles[8]"};
struct RemixCssEntry { std::string key,portrait; };
inline void descriptor_read(std::istream& input,RemixCssEntry& value) {
    descriptor_read(input,value.key);descriptor_read(input,value.portrait);
}
inline const DescriptorTable<RemixCssEntry> remix_css{"remix_css.tsv","key\tportrait"};
struct RemixAction {
    unsigned fighter;
    unsigned action;
    int animation;
    std::int64_t flags;
    int script;
};
inline void descriptor_read(std::istream& input,RemixAction& value) {
    descriptor_read(input,value.fighter);
    descriptor_read(input,value.action);
    descriptor_read(input,value.animation);
    descriptor_read(input,value.flags);
    descriptor_read(input,value.script);
}
inline const DescriptorTable<RemixAction> remix_actions{"remix_actions.tsv","fighter\taction\tanimation\tflags\tscript"};
struct RemixHitbox {
    unsigned script;
    unsigned begin;
    unsigned end;
    unsigned id;
    unsigned group;
    int joint;
    int damage;
    int size;
    int x;
    int y;
    int z;
    int angle;
    int growth;
    int weight;
    int base;
    int element;
    int ground_air;
    int shield_damage;
    int sound_kind;
    int sound_level;
    int scaled;
};
inline void descriptor_read(std::istream& input,RemixHitbox& value) {
    descriptor_read(input,value.script);
    descriptor_read(input,value.begin);
    descriptor_read(input,value.end);
    descriptor_read(input,value.id);
    descriptor_read(input,value.group);
    descriptor_read(input,value.joint);
    descriptor_read(input,value.damage);
    descriptor_read(input,value.size);
    descriptor_read(input,value.x);
    descriptor_read(input,value.y);
    descriptor_read(input,value.z);
    descriptor_read(input,value.angle);
    descriptor_read(input,value.growth);
    descriptor_read(input,value.weight);
    descriptor_read(input,value.base);
    descriptor_read(input,value.element);
    descriptor_read(input,value.ground_air);
    descriptor_read(input,value.shield_damage);
    descriptor_read(input,value.sound_kind);
    descriptor_read(input,value.sound_level);
    descriptor_read(input,value.scaled);
}
inline const DescriptorTable<RemixHitbox> remix_hitboxes{"remix_hitboxes.tsv","script\tbegin\tend\tid\tgroup\tjoint\tdamage\tsize\tx\ty\tz\tangle\tgrowth\tweight\tbase\telement\tground_air\tshield_damage\tsound_kind\tsound_level\tscaled"};
struct RemixEvent {
    unsigned script;
    unsigned frame;
    unsigned opcode;
    unsigned word_count;
    std::array<unsigned,5> words;
};
inline void descriptor_read(std::istream& input,RemixEvent& value) {
    descriptor_read(input,value.script);
    descriptor_read(input,value.frame);
    descriptor_read(input,value.opcode);
    descriptor_read(input,value.word_count);
    descriptor_read(input,value.words);
}
inline const DescriptorTable<RemixEvent> remix_events{"remix_events.tsv","script\tframe\topcode\tword_count\twords[0]\twords[1]\twords[2]\twords[3]\twords[4]"};
struct RemixScript {
    unsigned id;
    unsigned decoded;
};
inline void descriptor_read(std::istream& input,RemixScript& value) {
    descriptor_read(input,value.id);
    descriptor_read(input,value.decoded);
}
inline const DescriptorTable<RemixScript> remix_scripts{"remix_scripts.tsv","id\tdecoded"};

// Only self-contained, completely decoded scripts may expose combat windows.
// Fighter selection still requires ported callbacks and validated resource bindings.
inline std::vector<RemixHitbox> remix_hitboxes_at(unsigned script,unsigned frame) {
    const auto& definition=remix_scripts.at(script);
    if(definition.id!=script || !definition.decoded)
        throw std::runtime_error("Remix moveset requires linked-script/native callback port: "+std::to_string(script));
    std::vector<RemixHitbox> active;
    for(const auto& box:remix_hitboxes)
        if(box.script==script && frame>=box.begin && frame<box.end)active.push_back(box);
    return active;
}

inline const RemixFighter* remix_fighter(std::string_view key) {
    for (const auto& row:remix_roster) if (row.key==key) return &row;
    return nullptr;
}

inline int remix_action_id(const FighterSourceData& data,unsigned clip) {
    if (clip==data.idle || clip==data.selected) return 0x0a;
    if (clip==data.walks[0]) return 0x0b;
    if (clip==data.walks[1]) return 0x0c;
    if (clip==data.walks[2]) return 0x0d;
    if (clip==data.dash_clip) return 0x0f;
    if (clip==data.run_clip) return 0x10;
    if (clip==data.run_brake) return 0x11;
    if (clip==data.turn) return 0x12;
    if (clip==data.kneebend_clip) return 0x14;
    if (clip==data.jump) return 0x16;
    if (clip==data.jump_back) return 0x17;
    if (clip==data.aerial_forward) return 0x18;
    if (clip==data.aerial_back) return 0x19;
    if (clip==data.fall) return 0x1a;
    if (clip==data.crouch[0]) return 0x1c;
    if (clip==data.crouch[1]) return 0x1d;
    if (clip==data.crouch[2]) return 0x1e;
    if (clip==data.landing) return 0x1f;
    if (clip==data.landing_air[0]) return 0xd6;
    if (clip==data.landing_air[1]) return 0xd7;
    if (clip==data.landing_air[2]) return 0xd8;
    if (clip==data.landing_air[3]) return 0xd9;
    if (clip==data.landing_air[4]) return 0xda;
    if (clip==data.guard[0]) return 0x98;
    if (clip==data.guard[1]) return 0x9a;
    if (clip==data.guard[2]) return 0x9c;
    if (clip==data.guard[3]) return 0x9d;
    if (clip==data.cliff[0]) return 0x54;
    if (clip==data.taunt) return 0xbd;
    if (clip==data.jab) return 0xbe;
    if (clip==data.jab2) return 0xbf;
    if (clip==data.dash_attack) return 0xc0;
    if (clip==data.tilt[0]) return 0xc1;
    if (clip==data.tilt[1]) return 0xc2;
    if (clip==data.tilt[2]) return 0xc3;
    if (clip==data.tilt[3]) return 0xc4;
    if (clip==data.tilt[4]) return 0xc5;
    if (clip==data.tilt[5]) return 0xc7;
    if (clip==data.tilt[6]) return 0xc9;
    if (clip==data.smash[0]) return 0xcc;
    if (clip==data.smash[1]) return 0xcf;
    if (clip==data.smash[2]) return 0xd0;
    if (clip==data.attack_air[0]) return 0xd1;
    if (clip==data.attack_air[1]) return 0xd2;
    if (clip==data.attack_air[2]) return 0xd3;
    if (clip==data.attack_air[3]) return 0xd4;
    if (clip==data.attack_air[4]) return 0xd5;
    if (clip==data.grab[0]) return 0xa6;
    if (clip==data.grab[1]) return 0xa8;
    if (clip==data.grab[2]) return 0xa9;
    if (clip==data.grab[3]) return 0xaa;
    if (clip==data.capture[0]) return 0xab;
    if (clip==data.capture[1]) return 0xac;
    if (clip==data.special_start[0]) return 0xe0;
    if (clip==data.special_start[1]) return 0xe3;
    if (clip==data.special_start[2]) return 0xe6;
    if (clip==data.special_loop[0]) return 0xe1;
    if (clip==data.special_loop[1]) return 0xe4;
    if (clip==data.special_loop[2]) return 0xe7;
    if (clip==data.special_end[0]) return 0xe2;
    if (clip==data.special_end[1]) return 0xe5;
    if (clip==data.special_end[2]) return 0xe8;
    if (clip==data.special_active[0]) return 0xe0;
    if (clip==data.special_active[1]) return 0xe3;
    if (clip==data.special_active[2]) return 0xe6;
    return -1;
}

inline unsigned remix_motion_clip(std::string_view key,FighterKind kind,unsigned clip) {
    const auto* fighter=remix_fighter(key);
    if (!fighter || kind>=FighterKind::Count) return clip;
    const auto& data=fighter_source_data[static_cast<unsigned>(kind)];
    const int action=remix_action_id(data,clip);
    if (action<0) return clip;
    for (const auto& row:remix_actions)
        if (row.fighter==fighter->id && static_cast<int>(row.action)==action && row.animation>=0)
            return static_cast<unsigned>(row.animation);
    return clip;
}
}
