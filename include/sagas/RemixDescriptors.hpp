#pragma once
#include <sagas/FighterDescriptors.hpp>
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
}
