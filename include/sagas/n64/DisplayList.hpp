#pragma once

#include <sagas/n64/Archive.hpp>

namespace sagas::n64 {

struct Vertex {
    float x{}, y{}, z{};
    float u{}, v{};
    Vec3 normal{};
    bool lit{};
    Color color{255, 255, 255, 255};
    std::shared_ptr<const RasterImage> texture;
    std::uint8_t texture_mode_s{}, texture_mode_t{};
    std::uint8_t texture_mask_s{}, texture_mask_t{};
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::size_t commands{}, display_lists{}, rejected_triangles{}, unsupported_commands{};
};

// Interpreter: translates immutable F3DEX2 command streams to triangles.
class DisplayListDecoder final {
public:
    explicit DisplayListDecoder(RelocArchive& archive) : archive_(archive) {}
    [[nodiscard]] Mesh decode(Address display_list);
    [[nodiscard]] Mesh decode_links(Address links);
    [[nodiscard]] Mesh decode_pairs(Address pairs);
private:
    struct State;
    void list(Mesh& mesh, State& state, Address address, int depth);
    void triangle(Mesh& mesh, State& state, unsigned a, unsigned b, unsigned c);
    [[nodiscard]] std::shared_ptr<const RasterImage> texture(State& state);
    RelocArchive& archive_;
    std::unordered_map<std::string, std::shared_ptr<const RasterImage>> textures_;
};

} // namespace sagas::n64
