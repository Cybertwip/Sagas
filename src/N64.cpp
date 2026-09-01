#include <sagas/N64.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sagas::n64 {
namespace {

std::string reloc_name(std::uint32_t file, std::string_view suffix) {
    std::ostringstream name;
    name << "reloc/" << std::setw(4) << std::setfill('0') << file << suffix;
    return name.str();
}

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream stream(line);
    std::string value;
    while (std::getline(stream, value, '\t')) result.push_back(value);
    return result;
}

} // namespace

RelocArchive::RelocArchive(AssetRepository& assets) : assets_(assets) {
    std::ifstream input(assets_.path("reloc/symbols.tsv"));
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        const auto item = fields(line);
        if (item.size() == 3)
            symbols_.emplace(item[0], Address{static_cast<std::uint32_t>(std::stoul(item[1])),
                                               static_cast<std::uint32_t>(std::stoul(item[2]))});
    }
}

std::optional<Address> RelocArchive::symbol(std::string_view name) const {
    if (const auto found = symbols_.find(std::string(name)); found != symbols_.end()) return found->second;
    return {};
}

std::span<const std::byte> RelocArchive::bytes(std::uint32_t file) {
    if (auto found = files_.find(file); found != files_.end()) return *found->second;
    auto blob = assets_.blob(reloc_name(file, ".bin"));
    files_.emplace(file, blob);
    return *blob;
}

void RelocArchive::load_links(std::uint32_t file) {
    if (links_loaded_.contains(file)) return;
    links_loaded_[file] = true;
    std::ifstream input(assets_.path(reloc_name(file, ".links.tsv")));
    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        const auto item = fields(line);
        if (item.size() != 3) continue;
        const Address location{file, static_cast<std::uint32_t>(std::stoul(item[0]))};
        links_.emplace(key(location), Address{static_cast<std::uint32_t>(std::stoul(item[1])),
                                              static_cast<std::uint32_t>(std::stoul(item[2]))});
    }
}

std::optional<Address> RelocArchive::resolve(Address pointer_word) {
    load_links(pointer_word.file);
    if (const auto found = links_.find(key(pointer_word)); found != links_.end()) return found->second;
    return {};
}

std::uint32_t RelocArchive::u32(Address address) {
    const auto data = bytes(address.file);
    if (address.offset + 4 > data.size()) throw std::out_of_range("N64 u32 read");
    const auto* p = data.data() + address.offset;
    return (std::to_integer<std::uint32_t>(p[0]) << 24) |
           (std::to_integer<std::uint32_t>(p[1]) << 16) |
           (std::to_integer<std::uint32_t>(p[2]) << 8) | std::to_integer<std::uint32_t>(p[3]);
}
std::int16_t RelocArchive::s16(Address address) { return static_cast<std::int16_t>(u32(address) >> 16); }
float RelocArchive::f32(Address address) { return std::bit_cast<float>(u32(address)); }

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
                    out.vertex.color = state.lighting ? state.primitive :
                        Color{static_cast<std::uint8_t>(packed >> 24), static_cast<std::uint8_t>(packed >> 16),
                              static_cast<std::uint8_t>(packed >> 8), static_cast<std::uint8_t>(packed)};
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

std::vector<std::optional<Address>> AnimationDecoder::table(Address address, std::size_t count) {
    std::vector<std::optional<Address>> scripts;
    scripts.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Address word{address.file, address.offset + static_cast<std::uint32_t>(i * 4)};
        scripts.push_back(archive_.u32(word) == 0 ? std::nullopt : archive_.resolve(word));
    }
    return scripts;
}

JointPose AnimationDecoder::pose(const Node& node) {
    JointPose result;
    std::copy(node.rotate.begin(), node.rotate.end(), result.tracks.begin());
    std::copy(node.translate.begin(), node.translate.end(), result.tracks.begin() + 4);
    std::copy(node.scale.begin(), node.scale.end(), result.tracks.begin() + 7);
    return result;
}

void AnimationDecoder::apply(Node& node, const JointPose& pose) {
    std::copy_n(pose.tracks.begin(), 3, node.rotate.begin());
    std::copy_n(pose.tracks.begin() + 4, 3, node.translate.begin());
    std::copy_n(pose.tracks.begin() + 7, 3, node.scale.begin());
}

JointPose AnimationDecoder::sample(Address script, float frame, JointPose initial) {
    enum class Kind { None, Step, Linear, Cubic };
    struct Track {
        Kind kind{Kind::None};
        float base{}, target{}, rate_base{}, rate_target{}, start{}, duration{1};
        bool active{};
        [[nodiscard]] float value(float time) const {
            if (!active) return 0;
            const float length = std::clamp(time - start, 0.0f, duration);
            if (kind == Kind::Step) return length >= duration ? target : base;
            if (kind == Kind::Linear) return base + length * rate_base;
            if (kind != Kind::Cubic || duration == 0) return target;
            const float inv = 1.0f / duration;
            const float x2 = length * length;
            const float inv2 = inv * inv;
            const float x3_inv2 = x2 * length * inv2;
            const float twice = 2.0f * x3_inv2 * inv;
            const float thrice = 3.0f * x2 * inv2;
            const float x2_inv = x2 * inv;
            const float tangent = x3_inv2 - x2_inv;
            return base * ((twice - thrice) + 1.0f) + target * (thrice - twice) +
                   rate_base * ((tangent - x2_inv) + length) + rate_target * tangent;
        }
    };
    std::array<Track, 10> tracks{};
    Address command = script;
    float cursor{};
    std::size_t budget = 16'384;
    while (budget-- && cursor <= frame) {
        const auto word = archive_.u32(command);
        const unsigned opcode = word >> 25;
        const unsigned flags = (word >> 15) & 0x3ffU;
        const float duration = static_cast<float>(word & 0x7fffU);
        command.offset += 4;
        if (opcode == 0) break;
        if (opcode == 1 || opcode == 14) {
            const auto target = archive_.resolve(command);
            if (!target) break;
            command = *target;
            continue;
        }
        if (opcode == 2) { cursor += duration; continue; }
        if (opcode == 13) { command.offset += 4; continue; }
        if (opcode == 15 || opcode == 16) { cursor += duration; continue; }
        if (opcode == 17) {
            for (unsigned bit = 0; bit < 10; ++bit) if (flags & (1U << bit)) command.offset += 4;
            cursor += duration;
            continue;
        }
        const bool values = opcode == 3 || opcode == 4 || opcode == 5 || opcode == 6 ||
                            opcode == 8 || opcode == 9 || opcode == 10 || opcode == 11;
        if (opcode == 7) {
            for (unsigned bit = 0; bit < 10; ++bit) if (flags & (1U << bit)) {
                tracks[bit].rate_target = archive_.f32(command);
                command.offset += 4;
            }
            continue;
        }
        if (!values) break;
        for (unsigned bit = 0; bit < 10; ++bit) if (flags & (1U << bit)) {
            auto& track = tracks[bit];
            track.base = track.target;
            track.target = archive_.f32(command);
            command.offset += 4;
            track.start = cursor;
            track.duration = duration;
            track.active = true;
            if (opcode == 3 || opcode == 4) {
                track.kind = Kind::Linear;
                if (duration != 0) track.rate_base = (track.target - track.base) / duration;
                track.rate_target = 0;
            } else if (opcode == 5 || opcode == 6) {
                track.rate_base = track.rate_target;
                track.rate_target = archive_.f32(command);
                command.offset += 4;
                track.kind = Kind::Cubic;
            } else if (opcode == 8 || opcode == 9) {
                track.rate_base = track.rate_target;
                track.rate_target = 0;
                track.kind = Kind::Cubic;
            } else {
                track.rate_target = 0;
                track.kind = Kind::Step;
            }
        }
        if (opcode == 3 || opcode == 5 || opcode == 8 || opcode == 10) cursor += duration;
    }
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].active) initial.tracks[i] = tracks[i].value(frame);
    return initial;
}

} // namespace sagas::n64
