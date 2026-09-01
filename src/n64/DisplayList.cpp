#include <sagas/n64/DisplayList.hpp>

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace sagas::n64 {

struct DisplayListDecoder::State {
    struct Cached { Vertex vertex{}; bool valid{}; };
    struct Image { std::optional<Address> address; unsigned format{}, size{}, width{1}; } image;
    struct Load {
        Image image;
        unsigned uls{}, ult{}, lrs{}, lrt{};
        unsigned size{};
        bool block{};
    };
    struct Tile {
        unsigned format{}, size{}, line{}, tmem{}, palette{}, cms{}, cmt{}, masks{}, maskt{}, shifts{}, shiftt{};
        unsigned uls{}, ult{}, lrs{}, lrt{};
    };
    std::array<Cached, 32> cache{};
    std::array<Tile, 8> tiles{};
    std::unordered_map<unsigned, Load> loads;
    std::optional<Address> palette;
    unsigned render_tile{};
    float texture_scale_s{1}, texture_scale_t{1};
    bool lighting{true};
    Color primitive{255,255,255,255};
};

namespace {

Color rgba16(std::uint16_t value) {
    return {static_cast<std::uint8_t>(((value >> 11) & 31) * 255 / 31),
            static_cast<std::uint8_t>(((value >> 6) & 31) * 255 / 31),
            static_cast<std::uint8_t>(((value >> 1) & 31) * 255 / 31),
            static_cast<std::uint8_t>((value & 1) ? 255 : 0)};
}

} // namespace

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

Mesh DisplayListDecoder::decode_pairs(Address pairs) {
    Mesh result;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto address = archive_.resolve({pairs.file, pairs.offset + static_cast<std::uint32_t>(i * 4)});
        if (!address) continue;
        auto part = decode(*address);
        result.vertices.insert(result.vertices.end(), part.vertices.begin(), part.vertices.end());
        result.commands += part.commands;
        result.display_lists += part.display_lists;
        result.rejected_triangles += part.rejected_triangles;
        result.unsupported_commands += part.unsupported_commands;
    }
    return result;
}

void DisplayListDecoder::triangle(Mesh& mesh, State& state, unsigned a, unsigned b, unsigned c) {
    if (a >= state.cache.size() || b >= state.cache.size() || c >= state.cache.size() ||
        !state.cache[a].valid || !state.cache[b].valid || !state.cache[c].valid) {
        ++mesh.rejected_triangles;
        return;
    }
    const auto image = texture(state);
    const auto& tile = state.tiles[state.render_tile];
    const float width = image ? static_cast<float>(image->width) : 1.0f;
    const float height = image ? static_cast<float>(image->height) : 1.0f;
    const auto coordinate = [](float value, float extent, unsigned mode, unsigned mask) {
        if ((mode & 2U) != 0) return std::clamp(value / extent,0.0f,1.0f);
        const float period = mask ? static_cast<float>(1U << mask) : extent;
        float wrapped = std::fmod(value,period);
        if (wrapped < 0) wrapped += period;
        if ((mode & 1U) != 0) {
            const auto section = static_cast<int>(std::floor(value / period));
            if (section & 1) wrapped = period-wrapped;
        }
        return wrapped/extent;
    };
    for (const unsigned index : {a,b,c}) {
        auto vertex = state.cache[index].vertex;
        vertex.texture = image;
        float u=(vertex.u*state.texture_scale_s - tile.uls*0.25f);
        float v=(vertex.v*state.texture_scale_t - tile.ult*0.25f);
        if (tile.shifts) u *= tile.shifts <= 10 ? 1.0f/static_cast<float>(1U<<tile.shifts)
                                                : static_cast<float>(1U<<(16-tile.shifts));
        if (tile.shiftt) v *= tile.shiftt <= 10 ? 1.0f/static_cast<float>(1U<<tile.shiftt)
                                                : static_cast<float>(1U<<(16-tile.shiftt));
        vertex.u = coordinate(u,width,tile.cms,tile.masks);
        vertex.v = coordinate(v,height,tile.cmt,tile.maskt);
        mesh.vertices.push_back(std::move(vertex));
    }
}

std::shared_ptr<const RasterImage> DisplayListDecoder::texture(State& state) {
    const auto& tile = state.tiles[state.render_tile];
    const auto loaded = state.loads.find(tile.tmem);
    const auto image = loaded != state.loads.end() ? loaded->second.image : state.image;
    if (!image.address) return {};
    const unsigned bits = 4U << tile.size;
    unsigned width{};
    unsigned height{};
    unsigned source_x{};
    unsigned source_y{};
    unsigned source_row_bytes{};
    if (loaded != state.loads.end() && loaded->second.block) {
        // LoadBlock's lrs describes the amount copied to TMEM, while the
        // render tile's line describes its physical row stride.  TileSize
        // is a sampling/clamp window and can intentionally be much larger
        // than the source image, so it must not be used as image dimensions.
        const unsigned row_bytes = tile.line * 8U;
        const unsigned load_bits = 4U << loaded->second.size;
        const std::uint64_t load_units = loaded->second.lrs >= loaded->second.uls
            ? static_cast<std::uint64_t>(loaded->second.lrs - loaded->second.uls) + 1U : 0U;
        const std::uint64_t total_bits = load_units * load_bits;
        if (row_bytes != 0 && bits != 0) {
            width = row_bytes * 8U / bits;
            height = static_cast<unsigned>((total_bits + row_bytes * 8U - 1U) / (row_bytes * 8U));
            source_row_bytes = row_bytes;
        }
    } else if (loaded != state.loads.end()) {
        const auto& load = loaded->second;
        width = load.lrs >= load.uls ? ((load.lrs - load.uls) >> 2) + 1U : 0U;
        height = load.lrt >= load.ult ? ((load.lrt - load.ult) >> 2) + 1U : 0U;
        source_x = load.uls >> 2;
        source_y = load.ult >> 2;
        source_row_bytes = (image.width * bits + 7U) / 8U;
    }
    if (width == 0 || height == 0) {
        width = tile.lrs >= tile.uls ? ((tile.lrs - tile.uls) >> 2) + 1U : image.width;
        height = tile.lrt >= tile.ult ? ((tile.lrt - tile.ult) >> 2) + 1U : 1U;
    }
    if (width == 0 || height == 0 || width > 1024 || height > 1024) return {};
    if (source_row_bytes == 0) source_row_bytes = (width * bits + 7U) / 8U;
    std::ostringstream key;
    key << image.address->file << ':' << image.address->offset << ':' << tile.format << ':' << tile.size
        << ':' << width << ':' << height << ':' << source_x << ':' << source_y << ':'
        << source_row_bytes << ':' << tile.palette << ':';
    if (state.palette) key << state.palette->file << ':' << state.palette->offset;
    if (const auto found = textures_.find(key.str()); found != textures_.end()) return found->second;

    auto output = std::make_shared<RasterImage>();
    output->width = static_cast<int>(width);
    output->height = static_cast<int>(height);
    output->rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const auto source = archive_.bytes(image.address->file);
    const auto palette = state.palette ? archive_.bytes(state.palette->file) : std::span<const std::byte>{};
    auto byte = [](std::span<const std::byte> data, std::size_t at) -> unsigned {
        return at < data.size() ? std::to_integer<unsigned>(data[at]) : 0;
    };
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
        const std::size_t row = image.address->offset + static_cast<std::size_t>(source_y + y) * source_row_bytes;
        const unsigned source_pixel = source_x + x;
        Color color{255,0,255,255};
        unsigned intensity{}, alpha{255};
        if (tile.format == 0 && tile.size == 2) {
            color = rgba16(static_cast<std::uint16_t>((byte(source,row+source_pixel*2)<<8)|byte(source,row+source_pixel*2+1)));
        } else if (tile.format == 0 && tile.size == 3) {
            color = {static_cast<std::uint8_t>(byte(source,row+source_pixel*4)), static_cast<std::uint8_t>(byte(source,row+source_pixel*4+1)),
                     static_cast<std::uint8_t>(byte(source,row+source_pixel*4+2)), static_cast<std::uint8_t>(byte(source,row+source_pixel*4+3))};
        } else if (tile.format == 2 && state.palette) {
            unsigned index{};
            if (tile.size == 0) {
                const unsigned packed = byte(source,row+source_pixel/2);
                index = ((source_pixel & 1) ? (packed & 15) : (packed >> 4)) + tile.palette * 16;
            } else index = byte(source,row+source_pixel);
            const std::size_t at = state.palette->offset + index * 2;
            color = rgba16(static_cast<std::uint16_t>((byte(palette,at)<<8)|byte(palette,at+1)));
        } else if (tile.format == 3) {
            if (tile.size == 0) {
                const unsigned packed = byte(source,row+source_pixel/2);
                const unsigned value = (source_pixel & 1) ? (packed & 15) : (packed >> 4);
                intensity = ((value >> 1) & 7) * 255 / 7; alpha = (value & 1) ? 255 : 0;
            } else if (tile.size == 1) {
                const unsigned value = byte(source,row+source_pixel);
                intensity = (value >> 4) * 17; alpha = (value & 15) * 17;
            } else {
                intensity = byte(source,row+source_pixel*2); alpha = byte(source,row+source_pixel*2+1);
            }
            color = {static_cast<std::uint8_t>(intensity),static_cast<std::uint8_t>(intensity),
                     static_cast<std::uint8_t>(intensity),static_cast<std::uint8_t>(alpha)};
        } else if (tile.format == 4) {
            if (tile.size == 0) {
                const unsigned packed = byte(source,row+source_pixel/2);
                intensity = ((source_pixel & 1) ? (packed & 15) : (packed >> 4)) * 17;
            } else intensity = byte(source,row+source_pixel);
            color = {static_cast<std::uint8_t>(intensity),static_cast<std::uint8_t>(intensity),
                     static_cast<std::uint8_t>(intensity),255};
        }
        const auto out = (static_cast<std::size_t>(y) * width + x) * 4;
        output->rgba[out]=color.r; output->rgba[out+1]=color.g; output->rgba[out+2]=color.b; output->rgba[out+3]=color.a;
    }
    textures_.emplace(key.str(), output);
    return output;
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
            case 0xf1: case 0xfc:
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
                                            255};
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
                // F3DEX2 stores byte offsets (index * 2).  Shifting from
                // bits 17/9/1 decodes both the field and that factor of two.
                triangle(mesh, state, (w0 >> 17) & 0x7fU, (w0 >> 9) & 0x7fU,
                         (w0 >> 1) & 0x7fU);
                break;
            case 0x06: case 0x07:
                triangle(mesh, state, (w0 >> 17) & 0x7fU, (w0 >> 9) & 0x7fU,
                         (w0 >> 1) & 0x7fU);
                triangle(mesh, state, (w1 >> 17) & 0x7fU, (w1 >> 9) & 0x7fU,
                         (w1 >> 1) & 0x7fU);
                break;
            case 0xb1:
                // Legacy F3DEX TRI2 packs byte-sized indices multiplied by
                // ten rather than the F3DEX2 half-index representation.
                triangle(mesh, state, ((w0 >> 16) & 0xffU) / 10U, ((w0 >> 8) & 0xffU) / 10U,
                         (w0 & 0xffU) / 10U);
                triangle(mesh, state, ((w1 >> 16) & 0xffU) / 10U, ((w1 >> 8) & 0xffU) / 10U,
                         (w1 & 0xffU) / 10U);
                break;
            case 0xd9:
                state.lighting = (((w0 & 0x00ffffffU) | w1) & 0x00020000U) != 0;
                break;
            case 0xd7:
                state.render_tile = (w0 >> 8) & 7U;
                state.texture_scale_s = static_cast<float>((w1>>16)&0xffffU)/65536.0f;
                state.texture_scale_t = static_cast<float>(w1&0xffffU)/65536.0f;
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
            case 0xf0:
                state.palette = state.image.address;
                break;
            case 0xf2: {
                auto& tile = state.tiles[(w1 >> 24) & 7U];
                tile.uls=(w0>>12)&0xfffU; tile.ult=w0&0xfffU;
                tile.lrs=(w1>>12)&0xfffU; tile.lrt=w1&0xfffU;
                break;
            }
            case 0xf3: {
                const auto& tile = state.tiles[(w1 >> 24) & 7U];
                state.loads[tile.tmem] = {state.image, (w0 >> 12) & 0xfffU, w0 & 0xfffU,
                                          (w1 >> 12) & 0xfffU, 0, tile.size, true};
                break;
            }
            case 0xf4: {
                const auto& tile = state.tiles[(w1 >> 24) & 7U];
                state.loads[tile.tmem] = {state.image, (w0 >> 12) & 0xfffU, w0 & 0xfffU,
                                          (w1 >> 12) & 0xfffU, w1 & 0xfffU, tile.size, false};
                break;
            }
            case 0xf5: {
                auto& tile = state.tiles[(w1 >> 24) & 7U];
                tile.format=(w0>>21)&7U; tile.size=(w0>>19)&3U; tile.line=(w0>>9)&0x1ffU; tile.tmem=w0&0x1ffU;
                tile.palette=(w1>>20)&15U; tile.cmt=(w1>>18)&3U; tile.maskt=(w1>>14)&15U; tile.shiftt=(w1>>10)&15U;
                tile.cms=(w1>>8)&3U; tile.masks=(w1>>4)&15U; tile.shifts=w1&15U;
                break;
            }
            case 0xfd:
                state.image.format=(w0>>21)&7U; state.image.size=(w0>>19)&3U; state.image.width=(w0&0xfffU)+1;
                state.image.address=archive_.resolve({address.file,address.offset+4});
                break;
            default: ++mesh.unsupported_commands; break;
        }
    }
    throw std::runtime_error("N64 display-list command budget exceeded");
}

} // namespace sagas::n64
