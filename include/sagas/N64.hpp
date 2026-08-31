#pragma once

#include <sagas/Engine.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace sagas::n64 {

struct Address {
    std::uint32_t file{};
    std::uint32_t offset{};
    friend constexpr bool operator==(Address, Address) = default;
};

struct Vertex {
    float x{}, y{}, z{};
    float u{}, v{};
    Color color{255, 255, 255, 255};
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::size_t commands{}, display_lists{}, rejected_triangles{}, unsupported_commands{};
};

// Repository + Flyweight: one disk blob and relocation map per referenced file.
class RelocArchive final {
public:
    explicit RelocArchive(AssetRepository& assets);
    [[nodiscard]] std::optional<Address> symbol(std::string_view name) const;
    [[nodiscard]] std::optional<Address> resolve(Address pointer_word);
    [[nodiscard]] std::span<const std::byte> bytes(std::uint32_t file);
    [[nodiscard]] std::uint32_t u32(Address address);
    [[nodiscard]] std::int16_t s16(Address address);
    [[nodiscard]] float f32(Address address);
private:
    void load_links(std::uint32_t file);
    static std::uint64_t key(Address address) {
        return (static_cast<std::uint64_t>(address.file) << 32) | address.offset;
    }
    AssetRepository& assets_;
    std::unordered_map<std::string, Address> symbols_;
    std::unordered_map<std::uint32_t, std::shared_ptr<const std::vector<std::byte>>> files_;
    std::unordered_map<std::uint64_t, Address> links_;
    std::unordered_map<std::uint32_t, bool> links_loaded_;
};

// Interpreter pattern: translates immutable F3DEX2 command streams to triangles.
class DisplayListDecoder final {
public:
    explicit DisplayListDecoder(RelocArchive& archive) : archive_(archive) {}
    [[nodiscard]] Mesh decode(Address display_list);
private:
    struct State;
    void list(Mesh& mesh, State& state, Address address, int depth);
    void triangle(Mesh& mesh, State& state, unsigned a, unsigned b, unsigned c);
    RelocArchive& archive_;
};

struct Node {
    int depth{};
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

