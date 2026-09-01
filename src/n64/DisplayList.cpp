#include <sagas/n64/DisplayList.hpp>

#include <array>
#include <stdexcept>

namespace sagas::n64 {

struct DisplayListDecoder::State {
    struct Cached { Vertex vertex{}; bool valid{}; };
    std::array<Cached, 32> cache{};
    bool lighting{true};
    Color primitive{255,255,255,255};
};

Mesh DisplayListDecoder::decode(Address display_list) {
    Mesh mesh;
    State state;
    list(mesh, state, display_list, 0);
    return mesh;
}

Mesh DisplayListDecoder::decode_links(Address links) {
    Mesh result;
    for (std::size_t i = 0; i < 64; ++i, links.offset += 8) {
        const auto list_id = archive_.u32(links);
        if (list_id == 4) return result;
        const auto address = archive_.resolve({links.file, links.offset + 4});
        if (!address) continue;
        auto part = decode(*address);
        result.vertices.insert(result.vertices.end(), part.vertices.begin(), part.vertices.end());
        result.commands += part.commands;
        result.display_lists += part.display_lists;
        result.rejected_triangles += part.rejected_triangles;
        result.unsupported_commands += part.unsupported_commands;
    }
    throw std::runtime_error("unterminated N64 display-list links");
}

void DisplayListDecoder::triangle(Mesh& mesh, State& state, unsigned a, unsigned b, unsigned c) {
    if (a >= state.cache.size() || b >= state.cache.size() || c >= state.cache.size() ||
        !state.cache[a].valid || !state.cache[b].valid || !state.cache[c].valid) {
        ++mesh.rejected_triangles;
        return;
    }
    mesh.vertices.push_back(state.cache[a].vertex);
    mesh.vertices.push_back(state.cache[b].vertex);
    mesh.vertices.push_back(state.cache[c].vertex);
}

void DisplayListDecoder::list(Mesh& mesh, State& state, Address address, int depth) {
    if (depth >= 32) throw std::runtime_error("N64 display-list recursion limit");
    ++mesh.display_lists;
    for (std::size_t index = 0; index < 1'000'000; ++index, address.offset += 8) {
        ++mesh.commands;
        const auto w0 = archive_.u32(address);
        const auto w1 = archive_.u32({address.file, address.offset + 4});
        const auto opcode = w0 >> 24;
        switch (opcode) {
            case 0x00: case 0xe1: case 0xe3: case 0xe6: case 0xe7: case 0xe8: case 0xe9:
            case 0xf1: case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xfc: case 0xfd:
                break;
            case 0x01: {
                const unsigned count = (w0 >> 12) & 0xffU;
                const unsigned end = (w0 >> 1) & 0x7fU;
                const unsigned first = end >= count ? end - count : 32;
                const auto source = archive_.resolve({address.file, address.offset + 4});
                if (!source || count > 32 || first + count > 32) throw std::runtime_error("invalid N64 vertex load");
                for (unsigned i = 0; i < count; ++i) {
                    const Address vertex{source->file, source->offset + i * 16};
                    auto& out = state.cache[first + i];
                    out.vertex.x = archive_.s16(vertex);
                    out.vertex.y = archive_.s16({vertex.file, vertex.offset + 2});
                    out.vertex.z = archive_.s16({vertex.file, vertex.offset + 4});
                    out.vertex.u = archive_.s16({vertex.file, vertex.offset + 8}) / 32.0f;
                    out.vertex.v = archive_.s16({vertex.file, vertex.offset + 10}) / 32.0f;
                    const auto packed = archive_.u32({vertex.file, vertex.offset + 12});
                    out.vertex.lit = state.lighting;
                    if (state.lighting) {
                        const auto component = [packed](unsigned shift) {
                            return static_cast<float>(static_cast<std::int8_t>(packed >> shift)) / 127.0f;
                        };
                        out.vertex.normal = {component(24), component(16), component(8)};
                        out.vertex.color = {state.primitive.r, state.primitive.g, state.primitive.b,
                                            static_cast<std::uint8_t>(packed)};
                    } else {
                        out.vertex.color = Color{static_cast<std::uint8_t>(packed >> 24),
                            static_cast<std::uint8_t>(packed >> 16), static_cast<std::uint8_t>(packed >> 8),
                            static_cast<std::uint8_t>(packed)};
                    }
                    out.valid = true;
                }
                break;
            }
            case 0x05:
                triangle(mesh, state, (w0 >> 17) & 0x7fU, (w0 >> 9) & 0x7fU, (w0 >> 1) & 0x7fU);
                break;
            case 0x06: case 0xb1:
                triangle(mesh, state, (w0 >> 17) & 0x7fU, (w0 >> 9) & 0x7fU, (w0 >> 1) & 0x7fU);
                triangle(mesh, state, (w1 >> 17) & 0x7fU, (w1 >> 9) & 0x7fU, (w1 >> 1) & 0x7fU);
                break;
            case 0xd9:
                state.lighting = (((w0 & 0x00ffffffU) | w1) & 0x00020000U) != 0;
                break;
            case 0xde: {
                const auto target = archive_.resolve({address.file, address.offset + 4});
                if (!target) { ++mesh.unsupported_commands; break; }
                list(mesh, state, *target, depth + 1);
                if (w0 & 0x00010000U) return;
                break;
            }
            case 0xdf: return;
            case 0xfa:
                state.primitive = {static_cast<std::uint8_t>(w1 >> 24), static_cast<std::uint8_t>(w1 >> 16),
                                   static_cast<std::uint8_t>(w1 >> 8), static_cast<std::uint8_t>(w1)};
                break;
            default: ++mesh.unsupported_commands; break;
        }
    }
    throw std::runtime_error("N64 display-list command budget exceeded");
}

} // namespace sagas::n64
