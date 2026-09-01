#pragma once

#include <sagas/n64/SceneGraph.hpp>

namespace sagas::n64 {

struct JointPose {
    // RotXYZ, TraI, TraXYZ, ScaleXYZ: the ten native AObj joint tracks.
    std::array<float, 10> tracks{0, 0, 0, 0, 0, 0, 0, 1, 1, 1};
};

// Interpreter: evaluates the cartridge's AObjEvent32 command stream.
class AnimationDecoder final {
public:
    explicit AnimationDecoder(RelocArchive& archive) : archive_(archive) {}
    [[nodiscard]] std::vector<std::optional<Address>> table(Address address, std::size_t count);
    [[nodiscard]] JointPose sample(Address script, float frame, JointPose initial = {});
    [[nodiscard]] JointPose sample16(Address script, float frame, JointPose initial = {});
    static JointPose pose(const Node& node);
    static void apply(Node& node, const JointPose& pose);
private:
    RelocArchive& archive_;
};

} // namespace sagas::n64
