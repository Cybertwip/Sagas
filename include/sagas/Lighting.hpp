#pragma once

#include <sagas/Core.hpp>

namespace sagas {

struct DirectionalLight {
    Vec3 direction{-0.35f, 0.72f, 0.60f};
    Color color{255, 244, 224, 255};
    float intensity{0.58f};
};

struct SpotLight {
    Vec3 position{};
    Vec3 direction{0, -1, 0};
    Color color{255, 238, 202, 255};
    float intensity{};
    float range{1};
    // Cosines of the inner and outer half angles. Keeping these in the
    // descriptor avoids trigonometry in every fragment.
    float inner_cone{0.94f};
    float outer_cone{0.80f};
    bool enabled{};
};

struct LightingRig {
    Color ambient{176, 180, 192, 255};
    float ambient_intensity{0.62f};
    DirectionalLight key{};
    SpotLight spot{};
    Vec3 environment_up{0, 1, 0};
    Color reflection{205, 218, 236, 255};
    float reflection_intensity{0.18f};
    float shininess{9.0f};
};

class LightingSystem final {
public:
    [[nodiscard]] static Color shade(Color surface, Vec3 normal, Vec3 view_direction,
                                     const LightingRig& rig);
    [[nodiscard]] static LightingRig opening_room();
    [[nodiscard]] static LightingRig opening_room_at(int local);
    static void aim_opening_spotlight(LightingRig& rig, Vec3 emitter, float fade);
};

} // namespace sagas
