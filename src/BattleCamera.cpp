#include <sagas/Scene3D.hpp>
#include <sagas/FighterSourceData.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace sagas {
void BattleCamera::tick(std::span<const FighterBody> fighters,const Stage3D& stage) {
    unsigned count=0;
    for (const auto& body:fighters) if (body.stocks>0) ++count;
    constexpr float zooms[]{0,1.5f,1.32f,1.16f,1};
    float left=65536,right=-65536,bottom=65536,top=-65536;
    for (const auto& body:fighters) if (body.stocks>0) {
        const auto& data=fighter_source_data[static_cast<unsigned>(body.kind)];
        const auto& bounds=stage.camera_bounds;
        const float x=std::clamp(body.position.x,bounds[3],bounds[2]);
        const float y=std::clamp(body.position.y+data.cam_offset_y,bounds[1],bounds[0]);
        float zoom=zooms[std::min(count,4U)]*data.camera_zoom;
        if (body.status==FighterStatus::Wait && body.action_frame>=120) zoom*=.75f;
        left=std::min(left,x-(body.lr<0?1000:700)*zoom);
        right=std::max(right,x+(body.lr<0?700:1000)*zoom);
        bottom=std::min(bottom,y-700*zoom); top=std::max(top,y+700*zoom);
    }
    if (!count) {left=bottom=-2000;right=top=2000;}
    const float hz=(right-left)*.5f,vt=(top-bottom)*.5f;
    const float bias=std::clamp((std::max(hz,vt)-1000)/1000,0.f,1.f)*.0682f;
    const Vec3 target{(left+right)*.5f,(.5f-bias)*(bottom+top),0};
    constexpr float radians=std::numbers::pi_v<float>/180;
    const float tangent=std::tan(19*radians);
    // Source fits the interest rectangle to its viewport. Use the requested
    // 16:9 viewport here instead of the cartridge's 15:11 viewport.
    const float desired=std::clamp(std::max(vt/tangent,hz/(tangent*16/9)),2500.f,30000.f);
    distance_=desired>=distance_?desired:distance_-(distance_-desired)*.075f;
    const float pan=distance_>15000?.1f:distance_<2000?.05f:(1-(distance_-2000)/13000)*.05f+.05f;
    camera_.at.x+=(target.x-camera_.at.x)*pan;
    camera_.at.y+=(target.y-camera_.at.y)*pan;
    const float pitch=std::clamp(-(camera_.at.y-900)/133,-7.f,5.f)*radians+stage.camera_angle;
    const float yaw=std::clamp(-camera_.at.x/133,-17.5f,17.5f)*radians;
    const Vec3 direction{std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    camera_.eye.x+=(camera_.at.x+distance_*direction.x-camera_.eye.x)*.1f;
    camera_.eye.y+=(camera_.at.y+distance_*direction.y-camera_.eye.y)*.1f;
    camera_.eye.z+=(distance_*direction.z-camera_.eye.z)*.1f;
}
}
