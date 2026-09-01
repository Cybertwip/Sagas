#pragma once

#include <sagas/Core.hpp>

namespace sagas {

struct DirectionalLight {
    Vec3 direction{-0.35f, 0.72f, 0.60f};
    Color color{255, 244, 224, 255};
    float intensity{0.58f};
};

struct LightingRig {
    Color ambient{176, 180, 192, 255};
    float ambient_intensity{0.62f};
    DirectionalLight key{};
    Color reflection{205, 218, 236, 255};
    float reflection_intensity{0.18f};
    float shininess{9.0f};
};

class LightingSystem final {
public:
    [[nodiscard]] static Color shade(Color surface, Vec3 normal, Vec3 view_direction,
                                     const LightingRig& rig);
};

} // namespace sagas
