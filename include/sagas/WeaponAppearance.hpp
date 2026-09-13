#pragma once
#include <array>
#include <sagas/FighterDescriptors.hpp>
namespace sagas {
struct WeaponAppearance { unsigned file,offset,flags,palette,clear_environment,cycles; };
inline void descriptor_read(std::istream& input,WeaponAppearance& value) {
    descriptor_read(input,value.file);
    descriptor_read(input,value.offset);
    descriptor_read(input,value.flags);
    descriptor_read(input,value.palette);
    descriptor_read(input,value.clear_environment);
    descriptor_read(input,value.cycles);
}

inline const DescriptorTable<WeaponAppearance> weapon_appearance{"weapon_appearance.tsv","file\toffset\tflags\tpalette\tclear_environment\tcycles",16};
}
