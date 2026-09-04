#pragma once

#include <sagas/n64/Archive.hpp>

namespace sagas::n64 {

struct Material {
    std::optional<Address> sprites;
    std::optional<Address> palettes;
    std::optional<Address> image;
    std::optional<Address> palette;
    unsigned format{}, size{}, width{1}, height{1};
    unsigned block_format{}, block_size{};
    unsigned flags{};
    float texture_scale_s{1}, texture_scale_t{1};
    unsigned tile_uls{}, tile_ult{}, tile_lrs{}, tile_lrt{};
    Color primitive{255,255,255,255};
    bool set_primitive{};
    std::optional<Color> light1;
    std::optional<Color> light2;
};

struct Vertex {
    float x{}, y{}, z{};
    float u{}, v{};
    Vec3 normal{};
    bool lit{};
    Color color{255, 255, 255, 255};
    std::optional<Color> light1;
    std::optional<Color> light2;
    std::shared_ptr<const RasterImage> texture;
    std::uint8_t texture_mode_s{}, texture_mode_t{};
    std::uint8_t texture_mask_s{}, texture_mask_t{};
    std::uint16_t texture_window_s{}, texture_window_t{};
    std::uint16_t material_index{0xffffU};
    // F3DEX transforms a vertex when it enters the RSP cache, not when a
    // triangle is emitted.  Fighter joint-pair lists deliberately retain
    // cached vertices across matrix changes to skin the seams between parts.
    // Keep that load-time matrix binding on every expanded triangle vertex.
    std::uint16_t transform_node{0xffffU};
    bool transform_parent{};
    bool translucent{};
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::size_t commands{}, display_lists{}, rejected_triangles{}, unsupported_commands{}, material_commands{};
};

struct JointMeshes {
    std::vector<Mesh> before;
    std::vector<Mesh> after;
};

// Interpreter: translates immutable F3DEX2 command streams to triangles.
class DisplayListDecoder final {
public:
    explicit DisplayListDecoder(RelocArchive& archive) : archive_(archive) {}
    [[nodiscard]] std::vector<std::vector<Material>> materials(Address table, std::size_t count);
    [[nodiscard]] Mesh decode(Address display_list, std::span<const Material> materials = {});
    [[nodiscard]] Mesh decode_links(Address links, std::span<const Material> materials = {});
    [[nodiscard]] Mesh decode_pairs(Address pairs, std::span<const Material> materials = {});
    // Fighter DObj dls[0]/dls[1] execute as one RSP stream. In particular,
    // Master Hand's pre-matrix lists reuse vertices loaded by earlier joints.
    [[nodiscard]] JointMeshes decode_joint_tree(
        std::span<const std::optional<Address>> pairs,
        std::span<const std::vector<Material>> materials);
    [[nodiscard]] std::vector<Mesh> decode_model_tree(
        std::span<const std::optional<Address>> display_lists,
        std::span<const std::vector<Material>> materials, bool linked);
private:
    struct State;
    void apply_mobj(State& state, const Material& material);
    void links(Mesh& mesh, State& state, Address address);
    void list(Mesh& mesh, State& state, Address address, int depth);
    void triangle(Mesh& mesh, State& state, unsigned a, unsigned b, unsigned c);
    [[nodiscard]] std::shared_ptr<const RasterImage> texture(State& state);
    RelocArchive& archive_;
    std::unordered_map<std::string, std::shared_ptr<const RasterImage>> textures_;
};

} // namespace sagas::n64
