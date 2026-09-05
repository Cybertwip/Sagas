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
        bool window_set{};
    };
    std::array<Cached, 32> cache{};
    std::array<Tile, 8> tiles{};
    std::unordered_map<unsigned, Load> loads;
    std::optional<Address> palette;
    unsigned render_tile{};
    float texture_scale_s{1}, texture_scale_t{1};
    std::uint32_t geometry_mode{0x00020000U};
    bool lighting{true};
    bool texture_enabled{};
    bool primitive_rgb{};
    bool primitive_alpha{};
    Color primitive{255,255,255,255};
    Color blend{};
    Color environment{255,255,255,255};
    std::uint32_t other_mode_h{};
    unsigned alpha_compare{};
    N64RenderState rdp;
    std::optional<Color> light1;
    std::optional<Color> light2;
    std::optional<std::uint16_t> material_index;
    std::uint16_t transform_node{0xffffU};
    bool transform_parent{};
    std::uint32_t render_mode{};
    bool translucent{};
    std::span<const Material> materials;
};

namespace {

Color rgba16(std::uint16_t value) {
    return {static_cast<std::uint8_t>(((value >> 11) & 31) * 255 / 31),
            static_cast<std::uint8_t>(((value >> 6) & 31) * 255 / 31),
            static_cast<std::uint8_t>(((value >> 1) & 31) * 255 / 31),
            static_cast<std::uint8_t>((value & 1) ? 255 : 0)};
}

bool source_alpha_blend(std::uint32_t mode) {
    // Decode the two RDP blender cycles instead of treating every vertex
    // alpha byte as transparency.  XLU modes blend input colour by input
    // alpha with framebuffer colour by (1-alpha); opaque FORCE_BL modes use
    // a different denominator and remain depth-writing surfaces.
    const bool cycle1=((mode>>26)&3U)==0U && ((mode>>22)&3U)==1U && ((mode>>18)&3U)==0U;
    const bool cycle2=((mode>>24)&3U)==0U && ((mode>>20)&3U)==1U && ((mode>>16)&3U)==0U;
    return ((mode&0x0c00U)==0x0800U) || cycle1 || cycle2;
}

} // namespace

std::vector<std::vector<Material>> DisplayListDecoder::materials(Address table, std::size_t count) {
    std::vector<std::vector<Material>> result(count);
    const auto byte=[&](Address at) {
        const auto data=archive_.bytes(at.file);
        return at.offset<data.size() ? std::to_integer<unsigned>(data[at.offset]) : 0U;
    };
    const auto half=[&](Address at) {
        return static_cast<unsigned>(static_cast<std::uint16_t>(archive_.s16(at)));
    };
    for (std::size_t node=0;node<count;++node) {
        const Address slot{table.file,table.offset+static_cast<std::uint32_t>(node*4)};
        if (archive_.u32(slot)==0) continue;
        const auto list=archive_.resolve(slot);
        if (!list) continue;
        for (std::size_t index=0;index<16;++index) {
            const Address cell{list->file,list->offset+static_cast<std::uint32_t>(index*4)};
            if (archive_.u32(cell)==0) break;
            const auto sub=archive_.resolve(cell);
            if (!sub) break;
            Material material;
            const auto sprites=archive_.resolve({sub->file,sub->offset+4});
            material.sprites=sprites;
            if (sprites && archive_.u32(*sprites)!=0) material.image=archive_.resolve(*sprites);
            const auto palettes=archive_.resolve({sub->file,sub->offset+0x2c});
            material.palettes=palettes;
            if (palettes && archive_.u32(*palettes)!=0) material.palette=archive_.resolve(*palettes);
            material.format=byte({sub->file,sub->offset+2});
            material.size=byte({sub->file,sub->offset+3});
            material.width=std::max(half({sub->file,sub->offset+0x0c}),1U);
            material.height=std::max(half({sub->file,sub->offset+0x0e}),1U);
            const unsigned flags=half({sub->file,sub->offset+0x30});
            const float trau=archive_.f32({sub->file,sub->offset+0x14});
            const float trav=archive_.f32({sub->file,sub->offset+0x18});
            const float scau=archive_.f32({sub->file,sub->offset+0x1c});
            const float scav=archive_.f32({sub->file,sub->offset+0x20});
            const unsigned divisor=std::max(half({sub->file,sub->offset+8}),1U);
            const auto texture_scale=[](float scale,unsigned divisor) {
                if (std::abs(scale)<1.0e-8f) return 0.0f;
                return std::min((2097152.0f/divisor)/scale,65535.0f)/65536.0f;
            };
            material.texture_scale_s=texture_scale(scau,divisor);
            material.texture_scale_t=texture_scale(scav,divisor);
            const float safe_s=std::abs(scau)>1.0e-8f ? scau : 1.0f;
            const float safe_t=std::abs(scav)>1.0e-8f ? scav : 1.0f;
            const unsigned bias=half({sub->file,sub->offset+0x0a});
            material.tile_uls=static_cast<unsigned>(std::max((((material.width*trau)+bias)/safe_s)*4.0f,0.0f));
            material.tile_ult=static_cast<unsigned>(std::max(((((1.0f-scav)-trav)*material.height+bias)/safe_t)*4.0f,0.0f));
            material.tile_lrs=material.tile_uls+(material.width-1U)*4U;
            material.tile_lrt=material.tile_ult+(material.height-1U)*4U;
            material.primitive={static_cast<std::uint8_t>(byte({sub->file,sub->offset+0x50})),
                                static_cast<std::uint8_t>(byte({sub->file,sub->offset+0x51})),
                                static_cast<std::uint8_t>(byte({sub->file,sub->offset+0x52})),
                                static_cast<std::uint8_t>(byte({sub->file,sub->offset+0x53}))};
            material.flags=flags;
            material.block_format=byte({sub->file,sub->offset+0x32});
            material.block_size=byte({sub->file,sub->offset+0x33});
            material.set_primitive=(flags&(0x0200U|0x0010U|0x0008U))!=0;
            const auto packed_color=[&](std::uint32_t offset) {
                return Color{static_cast<std::uint8_t>(byte({sub->file,sub->offset+offset})),
                             static_cast<std::uint8_t>(byte({sub->file,sub->offset+offset+1})),
                             static_cast<std::uint8_t>(byte({sub->file,sub->offset+offset+2})),
                             static_cast<std::uint8_t>(byte({sub->file,sub->offset+offset+3}))};
            };
            if (flags&0x1000U) material.light1=packed_color(0x60);
            if (flags&0x2000U) material.light2=packed_color(0x64);
            result[node].push_back(material);
        }
    }
    return result;
}

void DisplayListDecoder::apply_mobj(State& state, const Material& material) {
    // Mirror gcDrawMObjForDObj in ssb-decomp-re src/sys/objdisplay.c.
    // 0xDE 0x0E...... is a branch into a heap DL HAL builds per MObj; we
    // apply the same SetTimg/LoadTLUT/tile effects without emitting Gfx.
    unsigned flags = material.flags;
    if (flags == 0) flags = 0x80U | 0x20U | 0x01U; // TEXTURE | tile | ALPHA
    if ((flags & 0x4U) && material.palette) {
        state.palette = material.palette;
        // This is the pending SetTextureImage source consumed by the
        // following LoadTLUT. It does not replace an existing texture load
        // in state.loads.
        state.image = {material.palette, 0, 2, 1};
    }
    if (material.set_primitive) state.primitive = material.primitive;
    if (material.light1) state.light1 = material.light1;
    if (material.light2) state.light2 = material.light2;
    if ((flags & (0x01U | 0x02U | 0x10U)) && material.image) {
        // The current sprite is selected by ALPHA or FRAC. SPLIT without
        // either selects the secondary block. MOBJ_FLAG_TEXTURE (0x80)
        // controls gSPTexture scaling only; it does not issue SetTextureImage.
        const bool current=(flags&(0x01U|0x10U))!=0;
        const unsigned fmt=current ? material.format : material.block_format;
        const unsigned siz=current ? material.size : material.block_size;
        state.image = {material.image, fmt, siz, material.width};
        state.texture_scale_s = material.texture_scale_s;
        state.texture_scale_t = material.texture_scale_t;
    }
    if (flags & 0x20U) {
        auto& tile = state.tiles[state.render_tile];
        tile.uls = material.tile_uls;
        tile.ult = material.tile_ult;
        tile.lrs = material.tile_lrs;
        tile.lrt = material.tile_lrt;
        tile.window_set = true;
    }
    if (flags & 0x80U) state.texture_enabled=true;
}

Mesh DisplayListDecoder::decode(Address display_list, std::span<const Material> materials) {
    Mesh mesh;
    State state;
    state.materials=materials;
    list(mesh, state, display_list, 0);
    return mesh;
}

Mesh DisplayListDecoder::decode_links(Address links, std::span<const Material> materials) {
    Mesh result;
    State state;
    state.materials=materials;
    this->links(result,state,links);
    return result;
}

void DisplayListDecoder::links(Mesh& mesh, State& state, Address address) {
    for (std::size_t i = 0; i < 64; ++i, address.offset += 8) {
        const auto list_id = archive_.u32(address);
        if (list_id == 4) return;
        const auto target = archive_.resolve({address.file, address.offset + 4});
        if (target) {
            state.translucent=(list_id&1U)!=0;
            list(mesh,state,*target,0);
        }
    }
    throw std::runtime_error("unterminated N64 display-list links");
}

Mesh DisplayListDecoder::decode_pairs(Address pairs, std::span<const Material> materials) {
    Mesh result;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto address = archive_.resolve({pairs.file, pairs.offset + static_cast<std::uint32_t>(i * 4)});
        if (!address) continue;
        auto part = decode(*address,materials);
        result.vertices.insert(result.vertices.end(), part.vertices.begin(), part.vertices.end());
        result.commands += part.commands;
        result.display_lists += part.display_lists;
        result.rejected_triangles += part.rejected_triangles;
        result.unsupported_commands += part.unsupported_commands;
        result.material_commands += part.material_commands;
    }
    return result;
}

JointMeshes DisplayListDecoder::decode_joint_tree(
    std::span<const std::optional<Address>> pairs,
    std::span<const std::vector<Material>> materials, bool fighter) {
    JointMeshes result;
    result.before.resize(pairs.size());
    result.after.resize(pairs.size());
    State state;
    state.other_mode_h=fighter ? 0x00100000U : 0;
    state.rdp.cycles=fighter ? 2 : 1;
    for (std::size_t node=0;node<pairs.size();++node) {
        state.materials=node<materials.size() ? std::span<const Material>(materials[node])
                                              : std::span<const Material>{};
        state.material_index.reset();
        if (!pairs[node]) continue;
        if (const auto before=archive_.resolve(*pairs[node])) {
            state.transform_node=static_cast<std::uint16_t>(node);
            state.transform_parent=true;
            list(result.before[node],state,*before,0);
        }
        if (const auto after=archive_.resolve({pairs[node]->file,pairs[node]->offset+4})) {
            state.transform_node=static_cast<std::uint16_t>(node);
            state.transform_parent=false;
            list(result.after[node],state,*after,0);
        }
    }
    return result;
}

std::vector<Mesh> DisplayListDecoder::decode_model_tree(
    std::span<const std::optional<Address>> display_lists,
    std::span<const std::vector<Material>> materials, bool linked, bool fighter) {
    std::vector<Mesh> result(display_lists.size());
    State state;
    state.other_mode_h=fighter ? 0x00100000U : 0;
    state.rdp.cycles=fighter ? 2 : 1;
    for (std::size_t node=0;node<display_lists.size();++node) {
        state.materials=node<materials.size() ? std::span<const Material>(materials[node])
                                              : std::span<const Material>{};
        state.material_index.reset();
        if (!display_lists[node]) continue;
        if (linked) links(result[node],state,*display_lists[node]);
        else list(result[node],state,*display_lists[node],0);
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
    for (const unsigned index : {a,b,c}) {
        auto vertex = state.cache[index].vertex;
        // MObj segment branches execute between vertex loads and triangle
        // commands in several fighter/room display lists.  Light and
        // primitive state therefore belongs to triangle emission time, not
        // to the earlier cache load.
        if (vertex.lit) {
            vertex.color=state.primitive_rgb ? state.primitive : Color{255,255,255,255};
            vertex.color.a=state.primitive_alpha ? state.primitive.a : 255;
            vertex.light1=state.light1;
            vertex.light2=state.light2;
        }
        vertex.rdp=state.rdp;
        vertex.rdp.cull_mode=state.geometry_mode&0x600U;
        vertex.rdp.primitive=state.primitive;
        vertex.rdp.environment=state.environment;
        vertex.rdp.texture_gen=(state.geometry_mode&0x40000U)!=0;
        vertex.rdp.texture_gen_linear=(state.geometry_mode&0x80000U)!=0;
        vertex.rdp.generated_scale={1024.0f*state.texture_scale_s/width,
                                    1024.0f*state.texture_scale_t/height};
        vertex.rdp.alpha_threshold=state.alpha_compare==1 ? state.blend.a/255.0f : 0;
        vertex.texture = image;
        float u=(vertex.u*state.texture_scale_s - tile.uls*0.25f);
        float v=(vertex.v*state.texture_scale_t - tile.ult*0.25f);
        if (tile.shifts) u *= tile.shifts <= 10 ? 1.0f/static_cast<float>(1U<<tile.shifts)
                                                : static_cast<float>(1U<<(16-tile.shifts));
        if (tile.shiftt) v *= tile.shiftt <= 10 ? 1.0f/static_cast<float>(1U<<tile.shiftt)
                                                : static_cast<float>(1U<<(16-tile.shiftt));
        // Preserve continuous coordinates through interpolation. N64
        // clamp/wrap/mirror is a sampling operation; doing it independently
        // at each vertex creates seams across texture-period boundaries.
        vertex.u = u/width;
        vertex.v = v/height;
        vertex.texture_mode_s=static_cast<std::uint8_t>(tile.cms);
        vertex.texture_mode_t=static_cast<std::uint8_t>(tile.cmt);
        vertex.texture_mask_s=static_cast<std::uint8_t>(tile.masks);
        vertex.texture_mask_t=static_cast<std::uint8_t>(tile.maskt);
        vertex.texture_window_s=static_cast<std::uint16_t>(
            tile.window_set && tile.lrs>=tile.uls ? ((tile.lrs-tile.uls)>>2)+1U
                                                  : (image ? image->width : 1U));
        vertex.texture_window_t=static_cast<std::uint16_t>(
            tile.window_set && tile.lrt>=tile.ult ? ((tile.lrt-tile.ult)>>2)+1U
                                                  : (image ? image->height : 1U));
        if (state.material_index) vertex.material_index=*state.material_index;
        // Unlit N64 vertices store RGBA, but most room combiners sample
        // coverage from the texel or primitive, not SHADE alpha.  Treating
        // every a<255 vertex as translucent discarded the floor (a=0 with
        // a live texture).  Only near-black contact shadows use vertex
        // alpha as coverage, and XLU surfaces with a=0 keep texel alpha.
        const bool contact_shadow=!vertex.lit && vertex.color.a<255 &&
            vertex.color.r<8 && vertex.color.g<8 && vertex.color.b<8;
        vertex.translucent=state.translucent || (!state.rdp.enabled && contact_shadow);
        if (!vertex.lit && vertex.color.a==0 && !contact_shadow) vertex.color.a=255;
        mesh.vertices.push_back(std::move(vertex));
    }
}

std::shared_ptr<const RasterImage> DisplayListDecoder::texture(State& state) {
    if (!state.texture_enabled) return {};
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
        } else if (tile.format == 2) {
            unsigned index{};
            if (tile.size == 0) {
                const unsigned packed = byte(source,row+source_pixel/2);
                index = ((source_pixel & 1) ? (packed & 15) : (packed >> 4)) + tile.palette * 16;
            } else index = byte(source,row+source_pixel);
            if (state.palette) {
                const std::size_t at = state.palette->offset + index * 2;
                color = rgba16(static_cast<std::uint16_t>((byte(palette,at)<<8)|byte(palette,at+1)));
            } else {
                const unsigned level = tile.size == 0 ? index * 17 : index;
                color = {static_cast<std::uint8_t>(level),static_cast<std::uint8_t>(level),
                         static_cast<std::uint8_t>(level),255};
            }
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
                     static_cast<std::uint8_t>(intensity),static_cast<std::uint8_t>(intensity)};
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
            case 0x00: case 0xe1: case 0xe6: case 0xe7: case 0xe8: case 0xe9:
            case 0xf1:
                break;
            case 0xfc: { // SetCombine: PRIMITIVE is not used by shade-only parts.
                state.rdp.enabled=true;
                state.rdp.combine_hi=w0&0xffffffU;
                state.rdp.combine_lo=w1;
                const auto rgb_uses_primitive=[](unsigned a,unsigned b,unsigned c,unsigned d) {
                    return a==3 || b==3 || c==3 || d==3;
                };
                state.primitive_rgb=rgb_uses_primitive((w0>>20)&15,(w1>>28)&15,(w0>>15)&31,(w1>>15)&7)
                    || rgb_uses_primitive((w0>>5)&15,(w1>>24)&15,w0&31,(w1>>6)&7);
                state.primitive_alpha=((w0>>12)&7)==3 || ((w1>>12)&7)==3
                    || ((w0>>9)&7)==3 || ((w1>>9)&7)==3
                    || ((w1>>21)&7)==3 || ((w1>>3)&7)==3
                    || ((w1>>18)&7)==3 || (w1&7)==3;
                break;
            }
            case 0xe3: { // F3DEX2 SetOtherModeH
                const unsigned length=(w0&255U)+1;
                const unsigned shift=32-((w0>>8)&255U)-length;
                if (length<=32 && shift<32) {
                    const auto mask=static_cast<std::uint32_t>(((1ULL<<length)-1)<<shift);
                    state.other_mode_h=(state.other_mode_h&~mask)|(w1&mask);
                    state.rdp.cycles=((state.other_mode_h>>20)&3U)==1 ? 2 : 1;
                }
                break;
            }
            case 0xe2: // F3DEX2 SetOtherModeL
                if ((w0&0xffffU)==0x1e01U) state.alpha_compare=w1&3U;
                if ((w0&0xffffU)==0x001cU) {
                    state.render_mode=w1;
                    state.translucent=source_alpha_blend(state.render_mode);
                }
                break;
            case 0xb9: // legacy F3DEX SetOtherModeL / SetRenderMode
                if ((w0&0xffffU)==0x031dU || (w0&0x00ffffffU)==0U) {
                    state.render_mode=w1;
                    state.translucent=source_alpha_blend(state.render_mode);
                }
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
                    out.vertex.transform_node=state.transform_node;
                    out.vertex.transform_parent=state.transform_parent;
                    const auto packed = archive_.u32({vertex.file, vertex.offset + 12});
                    out.vertex.lit = state.lighting;
                    if (state.lighting) {
                        const auto component = [packed](unsigned shift) {
                            return static_cast<float>(static_cast<std::int8_t>(packed >> shift)) / 127.0f;
                        };
                        out.vertex.normal = {component(24), component(16), component(8)};
                        // RGB bytes hold the normal; the fourth byte remains SHADE alpha.
                        out.vertex.color = state.primitive;
                        out.vertex.light1=state.light1;
                        out.vertex.light2=state.light2;
                    } else {
                        out.vertex.color = Color{static_cast<std::uint8_t>(packed >> 24),
                            static_cast<std::uint8_t>(packed >> 16), static_cast<std::uint8_t>(packed >> 8),
                            static_cast<std::uint8_t>(packed)};
                    }
                    out.vertex.shade=state.lighting ? Color{255,255,255,static_cast<std::uint8_t>(packed)} : out.vertex.color;
                    out.valid = true;
                }
                break;
            }
            case 0x02: { // F3DEX2 ModifyVtx
                const unsigned where=(w0>>16)&0xffU;
                const unsigned vertex=(w0&0xffffU)>>1;
                if (vertex>=state.cache.size() || !state.cache[vertex].valid) {
                    ++mesh.unsupported_commands;
                    break;
                }
                auto& out=state.cache[vertex].vertex;
                if (where==0x10U) { // G_MWO_POINT_RGBA / normal bytes
                    if (out.lit) {
                        const auto component=[w1](unsigned shift) {
                            return static_cast<float>(static_cast<std::int8_t>(w1>>shift))/127.0f;
                        };
                        out.normal={component(24),component(16),component(8)};
                    } else {
                        out.color={static_cast<std::uint8_t>(w1>>24),static_cast<std::uint8_t>(w1>>16),
                                   static_cast<std::uint8_t>(w1>>8),static_cast<std::uint8_t>(w1)};
                    }
                } else if (where==0x14U) { // G_MWO_POINT_ST
                    out.u=static_cast<std::int16_t>(w1>>16)/32.0f;
                    out.v=static_cast<std::int16_t>(w1)/32.0f;
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
                // F3DEX2 GeometryMode replaces the selected mode bits.  w0
                // is an AND mask, not a list of enabled modes; treating its
                // many set bits as flags made every unlit textured surface
                // look lit (and interpreted vertex RGBA as normals).
                state.geometry_mode=(state.geometry_mode & (w0&0x00ffffffU))|w1;
                state.lighting=(state.geometry_mode&0x00020000U)!=0;
                break;
            case 0xb6: // legacy F3DEX ClearGeometryMode
                state.geometry_mode&=~w1;
                state.lighting=(state.geometry_mode&0x00020000U)!=0;
                break;
            case 0xb7: // legacy F3DEX SetGeometryMode
                state.geometry_mode|=w1;
                state.lighting=(state.geometry_mode&0x00020000U)!=0;
                break;
            case 0xd7:
                state.render_tile = (w0 >> 8) & 7U;
                state.texture_scale_s = static_cast<float>((w1>>16)&0xffffU)/65536.0f;
                state.texture_scale_t = static_cast<float>(w1&0xffffU)/65536.0f;
                state.texture_enabled=(w0&0xffU)!=0;
                break;
            case 0xdb: {
                // F3DEX2 G_MOVEWORD. Smash Remix gfx_decode applies
                // gSPLightColor (G_MW_LIGHTCOL = 0x0A) as the part's
                // Lights1 diffuse/ambient. Master Hand's DLs have no
                // textures; ignoring these left the opening hand clay-white.
                if (((w0 >> 16) & 0xffU) == 0x0aU) {
                    const Color color{static_cast<std::uint8_t>(w1 >> 24),
                                      static_cast<std::uint8_t>(w1 >> 16),
                                      static_cast<std::uint8_t>(w1 >> 8), 255};
                    const unsigned offset = w0 & 0xffffU;
                    if (offset == 0 || offset == 4) state.light1 = color;
                    else if (offset == 0x18 || offset == 0x1c) state.light2 = color;
                }
                break;
            }
            case 0xde: {
                const auto target = archive_.resolve({address.file, address.offset + 4});
                if (!target) {
                    if ((w1>>24)==0x0eU) {
                        const std::size_t material_index=(w1&0x00ffffffU)/8U;
                        if (material_index<state.materials.size()) {
                            apply_mobj(state, state.materials[material_index]);
                            state.material_index=static_cast<std::uint16_t>(material_index);
                            ++mesh.material_commands;
                            break;
                        }
                    }
                    ++mesh.unsupported_commands;
                    break;
                }
                list(mesh, state, *target, depth + 1);
                if (w0 & 0x00010000U) return;
                break;
            }
            case 0xdf: return;
            case 0xf9: // SetBlendColor (alpha compare reference)
            case 0xfb: { // SetEnvColor
                auto& color=opcode==0xf9 ? state.blend : state.environment;
                color={static_cast<std::uint8_t>(w1>>24),static_cast<std::uint8_t>(w1>>16),
                       static_cast<std::uint8_t>(w1>>8),static_cast<std::uint8_t>(w1)};
                break;
            }
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
                tile.window_set=true;
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
