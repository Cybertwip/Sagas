// Generated from US WPAttributes by extract_weapon_data.py.
#pragma once
#include <array>
#include <sagas/FighterDescriptors.hpp>
namespace sagas {
struct WeaponSourceData { int size,damage,angle,growth,weight,base,element,sfx; };
inline void descriptor_read(std::istream& input,WeaponSourceData& value) {
    descriptor_read(input,value.size);
    descriptor_read(input,value.damage);
    descriptor_read(input,value.angle);
    descriptor_read(input,value.growth);
    descriptor_read(input,value.weight);
    descriptor_read(input,value.base);
    descriptor_read(input,value.element);
    descriptor_read(input,value.sfx);
}

inline const DescriptorTable<WeaponSourceData> weapon_source_data{"weapon_source_data.tsv","size\tdamage\tangle\tgrowth\tweight\tbase\telement\tsfx",16};
}
