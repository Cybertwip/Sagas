#include <sagas/Scene3D.hpp>
#include <sagas/RemixDescriptors.hpp>
#include <sagas/FighterSourceData.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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
    unsigned links=0;
    for (const auto& entry:std::filesystem::directory_iterator(assets.root()/"reloc")) {
        const auto name=entry.path().filename().string();
        if (name.size()<11 || name.substr(name.size()-10)!=".links.tsv") continue;
        const auto owner=static_cast<std::uint32_t>(std::stoul(name.substr(0,name.size()-10)));
        std::ifstream input(entry.path());std::string line;std::getline(input,line);
        while (std::getline(input,line)) {
            const auto tab1=line.find('\t'),tab2=line.rfind('\t');
            if (tab1==std::string::npos || tab2==tab1) continue;
            const auto target=static_cast<std::uint32_t>(std::stoul(line.substr(tab1+1,tab2-tab1-1)));
            const auto offset=static_cast<std::uint32_t>(std::stoul(line.substr(tab2+1)));
            assert(offset<archive.bytes(target).size());
            ++links;(void)owner;
        }
    }
    const auto falco=std::find_if(sagas::remix_roster.begin(),sagas::remix_roster.end(),
        [](const auto& row){return row.key=="FALCO";});
    assert(falco!=sagas::remix_roster.end());
    sagas::Scene3DLoader loader(archive);
    const auto spec=sagas::fighter_model_spec(sagas::FighterKind::Fox);
    const auto common=archive.resolve({falco->files[0],falco->attribute_offset+0x2d4});
    assert(common);
    const auto tree=archive.resolve(*common);
    assert(tree);
    auto model=loader.fighter_model(*tree,sagas::GeometryLayout::Direct,spec.setup_parts,0,0,
        sagas::n64::Address{falco->files[0],falco->attribute_offset});
    assert(model.nodes.size()>=20);
    auto posed=loader.fighter_motion(sagas::FighterKind::Fox,sagas::fighter_source_data[9].idle,0,"FALCO");
    assert(!posed.meshes.empty());
    auto bowser=loader.fighter_motion(sagas::FighterKind::Yoshi,sagas::fighter_source_data[7].idle,0,"BOWSER");
    assert(!bowser.meshes.empty());
    unsigned bowser_tris=0;for (const auto& mesh:bowser.meshes) bowser_tris+=mesh.vertices.size();
    assert(bowser_tris>0);
    auto peach=loader.fighter_motion(sagas::FighterKind::Fox,sagas::fighter_source_data[9].idle,0,"PEACH");
    assert(peach.nodes.size()>=26);
    auto gbowser=loader.fighter_motion(sagas::FighterKind::Yoshi,sagas::fighter_source_data[7].idle,0,"GBOWSER");
    assert(!gbowser.meshes.empty());
    const auto attr=loader.remix_fighter_attributes("FALCO");
    assert(std::isfinite(attr.size) && attr.size>0 && attr.gravity>0);
    std::cout<<"Validated "<<count<<" Remix fighter attribute blocks and skeletons, "<<links<<" in-range relocations, Falco model\n";
}
