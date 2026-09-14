#include <sagas/Scene3D.hpp>
#include <sagas/RemixDescriptors.hpp>
#include <cassert>
#include <cmath>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    sagas::AssetRepository assets(argv[1]);
    sagas::n64::RelocArchive archive(assets);
    unsigned count=0;
    for(const auto& fighter:sagas::remix_roster) {
        const sagas::n64::Address attributes{fighter.files[0],fighter.attribute_offset};
        const float size=archive.f32(attributes);
        assert(std::isfinite(size) && size>0);
        const auto common=archive.resolve({attributes.file,attributes.offset+0x2d4});
        assert(common);
        const auto tree=archive.resolve(*common);
        assert(tree);
        const auto nodes=sagas::n64::SkeletonDecoder(archive).decode(*tree);
        assert(!nodes.empty());
        ++count;
    }
    std::cout<<"Validated "<<count<<" Remix fighter attribute blocks and skeletons\n";
}
