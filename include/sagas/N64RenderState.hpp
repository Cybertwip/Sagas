#pragma once

#include <sagas/Core.hpp>

namespace sagas {

// RDP state that must survive mesh decoding and draw batching. The two mux
// words describe (A-B)*C+D independently for RGB and alpha in each cycle.
struct N64RenderState {
    std::uint32_t combine_hi{}, combine_lo{};
    Color primitive{255,255,255,255};
    Color environment{255,255,255,255};
    Color tint{255,255,255,255};
    Vec2 generated_scale{1,1};
    float alpha_threshold{};
    unsigned cycles{1};
    bool enabled{}, texture_gen{}, texture_gen_linear{};
};

} // namespace sagas
