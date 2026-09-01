#include <sagas/n64/SceneGraph.hpp>

#include <stdexcept>

namespace sagas::n64 {

std::vector<Node> SkeletonDecoder::decode(Address descriptor, std::size_t limit) {
    std::vector<Node> nodes;
    nodes.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i, descriptor.offset += 44) {
        const auto id = archive_.u32(descriptor);
        if ((id & 0xfffU) == 18U) return nodes;
        Node node;
        node.depth = static_cast<int>(id & 0xfffU);
        node.flags = id & 0xf000U;
        node.display_list = archive_.resolve({descriptor.file, descriptor.offset + 4});
        for (int axis = 0; axis < 3; ++axis) {
            node.translate[axis] = archive_.f32({descriptor.file, descriptor.offset + 8U + axis * 4U});
            node.rotate[axis] = archive_.f32({descriptor.file, descriptor.offset + 20U + axis * 4U});
            node.scale[axis] = archive_.f32({descriptor.file, descriptor.offset + 32U + axis * 4U});
        }
        nodes.push_back(node);
    }
    throw std::runtime_error("unterminated N64 skeleton descriptor");
}

} // namespace sagas::n64
