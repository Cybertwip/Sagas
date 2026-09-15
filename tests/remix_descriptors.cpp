#include <sagas/RemixDescriptors.hpp>
#include <sagas/Fighter.hpp>
#include <cassert>
#include <algorithm>
#include <array>
int main() {
    using namespace sagas;
    const auto falco=std::find_if(remix_roster.begin(),remix_roster.end(),[](const auto& row){return row.key=="FALCO";});
    assert(falco!=remix_roster.end());
    assert(std::any_of(remix_css.begin(),remix_css.end(),[](const auto& row){return row.key=="FALCO";}));
    assert(falco->id==0x1d && falco->parent==static_cast<unsigned>(FighterKind::Fox));
    assert(falco->files[0]==0x8ab && falco->attribute_offset==0x474);
    const auto action=std::find_if(remix_actions.begin(),remix_actions.end(),[&](const auto& row){return row.fighter==falco->id && row.action==0xbe;});
    assert(action!=remix_actions.end() && action->script>=0);
    assert(remix_hitboxes_at(action->script,2).empty());
    const auto boxes=remix_hitboxes_at(action->script,3);
    assert(boxes.size()==2 && boxes.front().damage==4);
    assert(remix_hitboxes_at(action->script,5).empty());
    // Imported binary hitbox fields feed the native combat resolver.
    std::array<FighterBody,2> fighters{};
    fighters[0].kind=FighterKind::Fox;
    for(auto& fighter:fighters) {fighter.attr=fighter_attributes(fighter.kind);fighter.grounded=true;}
    const auto& box=boxes.front();
    AttackVolume volume{0,{0,fighters[1].attr.height*.5f,0},box.size*.5f,
        box.damage,box.angle,box.growth,box.weight,box.base,0,false,box.group,0,static_cast<unsigned>(box.element)};
    assert(FighterCombat::resolve(fighters,std::span<const AttackVolume>(&volume,1)).size()==1);
    assert(fighters[1].damage==4);
    bool checked=false;
    for(const auto& script:remix_scripts)if(!script.decoded) {
        bool rejected=false;
        try {remix_hitboxes_at(script.id,0);}catch(const std::runtime_error&){rejected=true;}
        assert(rejected);checked=true;break;
    }
    assert(checked);
}
