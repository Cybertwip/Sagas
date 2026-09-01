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
    const float reflection=std::pow(std::max(dot(normal,half_vector),0.0f),6.0f)*0.12f;
    const auto one = [&](std::uint8_t base, std::uint8_t ambient, std::uint8_t direct) {
        const float illumination = ambient * rig.ambient_intensity + direct * (diffuse+reflection);
        return channel(base * illumination / 255.0f);
    };
    return {one(surface.r, rig.ambient.r, rig.key.color.r),
            one(surface.g, rig.ambient.g, rig.key.color.g),
            one(surface.b, rig.ambient.b, rig.key.color.b), surface.a};
}

} // namespace sagas
