#include <sagas/Lighting.hpp>

#include <algorithm>
#include <cmath>

namespace sagas {
namespace {

float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

Vec3 normalize(Vec3 value) {
    const float length = std::sqrt(std::max(dot(value, value), 1.0e-12f));
    return {value.x / length, value.y / length, value.z / length};
}

std::uint8_t channel(float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 255.0f));
}

} // namespace

Color LightingSystem::shade(Color surface, Vec3 normal, Vec3 view_direction, const LightingRig& rig) {
    normal = normalize(normal);
    const Vec3 light = normalize(rig.key.direction);
    view_direction = normalize(view_direction);
    // The N64 fighter/scene setup uses a strong ambient term and broad
    // vertex-light falloff.  A wrapped diffuse lobe avoids the black/white
    // discontinuity of a hard one-sided Lambert term on low-poly meshes.
    const float facing = dot(normal, light);
    const float wrapped = std::clamp((facing + 0.45f) / 1.45f, 0.0f, 1.0f);
    const float diffuse = wrapped*wrapped*(3.0f-2.0f*wrapped) * rig.key.intensity;
    const Vec3 half_vector=normalize({light.x+view_direction.x,light.y+view_direction.y,
                                      light.z+view_direction.z});
    const float specular=std::pow(std::max(dot(normal,half_vector),0.0f),
                                  std::max(rig.shininess,1.0f));
    const float view_facing=std::max(dot(normal,view_direction),0.0f);
    const float fresnel=0.04f+0.20f*std::pow(1.0f-view_facing,5.0f);
    const float reflected=(specular*0.72f+fresnel*0.28f)*rig.reflection_intensity;
    const auto one = [&](std::uint8_t base, std::uint8_t ambient, std::uint8_t direct,
                         std::uint8_t reflection) {
        const float illumination = ambient * rig.ambient_intensity + direct * diffuse;
        return channel((base * illumination) / 255.0f + reflection*reflected);
    };
    return {one(surface.r, rig.ambient.r, rig.key.color.r,rig.reflection.r),
            one(surface.g, rig.ambient.g, rig.key.color.g,rig.reflection.g),
            one(surface.b, rig.ambient.b, rig.key.color.b,rig.reflection.b), surface.a};
}

} // namespace sagas
