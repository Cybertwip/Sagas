#pragma once

#include <sagas/Core.hpp>

namespace sagas {

struct DirectionalLight {
    Vec3 direction{-0.35f, 0.72f, 0.60f};
    Color color{255, 244, 224, 255};
    float intensity{0.82f};
};

struct LightingRig {
    Color ambient{118, 126, 145, 255};
    float ambient_intensity{0.42f};
    DirectionalLight key{};
};

class LightingSystem final {
public:
    [[nodiscard]] static Color shade(Color surface, Vec3 normal, const LightingRig& rig);
};

} // namespace sagas
