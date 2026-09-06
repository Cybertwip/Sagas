#pragma once

#include <sagas/n64/SceneGraph.hpp>

namespace sagas::n64 {

struct JointPose {
    // RotXYZ, TraI, TraXYZ, ScaleXYZ: the ten native AObj joint tracks.
    std::array<float, 10> tracks{0, 0, 0, 0, 0, 0, 0, 1, 1, 1};
    std::uint16_t flags{};
};

struct MaterialPose {
    // Texture IDs, UV transforms/scroll, blend fraction, and palette ID.
    std::array<float,10> tracks{0,0,0,1,1,0,0,0,0,0};
    // Primitive, environment, blend, light 1, and light 2 colours.
    std::array<Color,5> colors{{{255,255,255,255},{0,0,0,255},{0,0,0,0},
                                {255,255,255,255},{255,255,255,255}}};
};

// Interpreter: evaluates the cartridge's AObjEvent32 command stream.
class AnimationDecoder final {
public:
    explicit AnimationDecoder(RelocArchive& archive) : archive_(archive) {}
    [[nodiscard]] std::vector<std::optional<Address>> table(Address address, std::size_t count);
    [[nodiscard]] JointPose sample(Address script, float frame, JointPose initial = {});
    [[nodiscard]] JointPose sample16(Address script, float frame, JointPose initial = {}, float* end_frame = nullptr);
    [[nodiscard]] MaterialPose sample_material(Address script, float frame, MaterialPose initial = {});
    static JointPose pose(const Node& node);
    static void apply(Node& node, const JointPose& pose);
private:
    RelocArchive& archive_;
};

} // namespace sagas::n64
