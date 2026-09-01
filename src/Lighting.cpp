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

Color LightingSystem::shade(Color surface, Vec3 normal, const LightingRig& rig) {
    normal = normalize(normal);
    const Vec3 light = normalize(rig.key.direction);
    const float diffuse = std::max(0.0f, dot(normal, light)) * rig.key.intensity;
    const auto one = [&](std::uint8_t base, std::uint8_t ambient, std::uint8_t direct) {
        const float illumination = ambient * rig.ambient_intensity + direct * diffuse;
        return channel(base * illumination / 255.0f);
    };
    return {one(surface.r, rig.ambient.r, rig.key.color.r),
            one(surface.g, rig.ambient.g, rig.key.color.g),
            one(surface.b, rig.ambient.b, rig.key.color.b), surface.a};
}

} // namespace sagas
