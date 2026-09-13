#include <sagas/FighterDescriptors.hpp>
namespace sagas {
namespace {
std::filesystem::path root=SAGAS_DEFAULT_ASSET_ROOT "/fighters";
unsigned generation=1;
}
void set_fighter_descriptor_root(const std::filesystem::path& asset_root) {
    const auto next=asset_root/"fighters";
    if(root!=next) {root=next;++generation;}
}
const std::filesystem::path& fighter_descriptor_root() {return root;}
unsigned fighter_descriptor_generation() {return generation;}
}
