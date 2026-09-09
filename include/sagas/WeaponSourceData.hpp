// Generated from US WPAttributes by extract_weapon_data.py.
#pragma once
#include <array>
namespace sagas {
struct WeaponSourceData { int size,damage,angle,growth,weight,base,element,sfx; };
inline constexpr std::array<WeaponSourceData,7> weapon_source_data{{
    {200,6,361,25,0,10,1,28}, // Luigi
    {200,7,361,25,0,10,1,28}, // Mario
    {40,6,10,100,1,0,0,2}, // Fox
    {0,0,361,100,0,0,2,0}, // Samus
    {200,9,70,30,0,55,0,31}, // Link
    {200,4,80,50,0,40,1,2}, // Ness
    {200,10,361,30,0,50,2,23}, // Pikachu
}};
}
