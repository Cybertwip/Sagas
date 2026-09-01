#pragma once

#include <sagas/n64/Archive.hpp>

#include <array>

namespace sagas::n64 {

struct Node {
    int depth{};
    std::uint32_t flags{};
    Vec2 unused_2d{};
    std::array<float, 3> translate{}, rotate{}, scale{};
    std::optional<Address> display_list;
};

class SkeletonDecoder final {
public:
    explicit SkeletonDecoder(RelocArchive& archive) : archive_(archive) {}
    [[nodiscard]] std::vector<Node> decode(Address descriptor, std::size_t limit = 256);
private:
    RelocArchive& archive_;
};

} // namespace sagas::n64
