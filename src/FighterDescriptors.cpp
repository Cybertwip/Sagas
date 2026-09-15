#include <sagas/FighterDescriptors.hpp>
namespace sagas {
namespace {
std::filesystem::path fighter_root=SAGAS_DEFAULT_ASSET_ROOT "/fighters";
std::filesystem::path scene_root=SAGAS_DEFAULT_ASSET_ROOT "/scenes";
unsigned generation=1;
}
void set_fighter_descriptor_root(const std::filesystem::path& asset_root) {
    const auto next_fighters=asset_root/"fighters";
    const auto next_scenes=asset_root/"scenes";
    if(fighter_root!=next_fighters || scene_root!=next_scenes) {
        fighter_root=next_fighters;scene_root=next_scenes;++generation;
    }
}
const std::filesystem::path& fighter_descriptor_root() {return fighter_root;}
const std::filesystem::path& scene_descriptor_root() {return scene_root;}
unsigned fighter_descriptor_generation() {return generation;}
}
