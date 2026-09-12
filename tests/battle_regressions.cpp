#include <sagas/Scene3D.hpp>
#include <sagas/FighterSourceData.hpp>
#include <sagas/FighterAttackData.hpp>
#include <sagas/BattleCallbackData.hpp>
#include <sagas/WeaponSourceData.hpp>
#include <cassert>
#include <iostream>
#include <cmath>
using namespace sagas;
int main() {
    AssetRepository assets(SAGAS_DEFAULT_ASSET_ROOT);
    n64::RelocArchive archive(assets);
    Scene3DLoader loader(archive);Scene3DRenderer renderer(archive);
    for(unsigned kind=0;kind<12;++kind) {
        const auto& data=fighter_source_data[kind];
        auto model=loader.fighter_motion(static_cast<FighterKind>(kind),data.grab[0],fighter_motion_flags(data.grab[0]));
        model.rotation.y=1.57079632679f;model.scale={data.size,data.size,data.size};
        bool grabbed=false;
        for(const auto& box:source_jab_hitboxes) if(box.kind==kind && box.motion==data.grab[0]) {
            const auto p=renderer.joint_point(model,box.begin,box.joint,{float(box.x),float(box.y),float(box.z)});

            std::array<FighterBody,2> fighters{};
            fighters[0].kind=static_cast<FighterKind>(kind);fighters[0].attr=fighter_attributes(fighters[0].kind);
            fighters[1].attr=fighter_attributes(fighters[1].kind);fighters[1].position={p.x,0,0};
            assert(FighterCombat::start_grab(fighters[0],true));
            const AttackVolume hit{0,p,box.radius*.5f*data.size,0,0,0,0,0,0,true};
            (void)FighterCombat::resolve(fighters,std::span<const AttackVolume>(&hit,1));
            grabbed|=fighters[1].status==FighterStatus::Captured;
        }
        assert(grabbed);
        for(unsigned victim=0;victim<12;++victim) for(int direction=0;direction<2;++direction) {
            const auto clips=thrown_clips[kind][victim][direction];
            assert(clips.first || clips.next);
            auto captive=loader.fighter_motion(static_cast<FighterKind>(victim),clips.first?clips.first:clips.next,fighter_motion_flags(clips.first?clips.first:clips.next));
            const float scale=fighter_source_data[victim].size;captive.scale={scale,scale,scale};
            auto holder=loader.fighter_motion(static_cast<FighterKind>(kind),data.grab[direction+2],fighter_motion_flags(data.grab[direction+2]));
            holder.scale={data.size,data.size,data.size};
            for(int facing:{-1,1}) for(int frame:{0,5,9}) {
                holder.rotation.y=facing*1.57079632679f;holder.position={170,250,0};
                auto attached=renderer.captured_at_joint(captive,frame,holder,frame,data.capture_joint);
                assert(attached.root_transform);
                for(float x:*attached.root_transform)assert(std::isfinite(x));
                // Translating the holder translates the attached victim identically.
                holder.position.x+=137;
                auto moved=renderer.captured_at_joint(captive,frame,holder,frame,data.capture_joint);
                assert(std::abs((*moved.root_transform)[3]-(*attached.root_transform)[3]-137)<.02f);
                holder.position.x-=137;
            }
        }
    }
    {
        std::array<FighterBody,2> bodies{};for(auto& b:bodies)b.attr=fighter_attributes(b.kind);
        bodies[0].kind=FighterKind::Purin;
        AttackVolume sing{0,{0,200,0},1000,0,0,0,0,0,0,false,0,~0U,6};
        bodies[1].grounded=false;
        assert(FighterCombat::resolve(bodies,std::span<const AttackVolume>(&sing,1)).empty());
        bodies[1].grounded=true;bodies[1].status=FighterStatus::Shield;
        assert(FighterCombat::resolve(bodies,std::span<const AttackVolume>(&sing,1)).empty());
        bodies[1].status=FighterStatus::Wait;
        assert(FighterCombat::resolve(bodies,std::span<const AttackVolume>(&sing,1)).size()==1);
        assert(bodies[1].status==FighterStatus::Sleep && bodies[1].damage==0 && bodies[1].sleep_tics==375);
        for(int i=0;i<375;++i)FighterPhysics::tick(bodies[1],0);
        assert(bodies[1].status==FighterStatus::Wait);
    }
    {
        FighterBody kirby;kirby.kind=FighterKind::Kirby;kirby.attr=fighter_attributes(kirby.kind);kirby.stick_y=80;
        assert(FighterCombat::start_special(kirby,true));
        FighterCombat::advance_special(kirby,false,true);
        assert(kirby.special_phase==1 && !kirby.grounded && kirby.special_motion);
        kirby.position.y=1;
        FighterPhysics::tick(kirby,0);
        assert(kirby.grounded && kirby.special_phase==2 && kirby.special_motion==fighter_source_data[8].special_end[1]);
        kirby.action_frame=3;assert(FighterCombat::special_flag(kirby,0)==1);
        assert(weapon_source_data[9].damage>0);
        auto blade=loader.weapon({229,8},3);assert(!blade.nodes.empty());
        auto laser=loader.weapon({210,0},0);assert(!laser.meshes.empty());
    }
    std::cout<<"Battle regressions passed\n";
}
