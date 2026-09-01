#include <sagas/n64/Animation.hpp>

#include <algorithm>

namespace sagas::n64 {

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
    for (std::size_t i = 0; i < tracks.size(); ++i)
        tracks[i].base = tracks[i].target = initial.tracks[i];
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

JointPose AnimationDecoder::sample16(Address script, float frame, JointPose initial) {
    enum class Kind { None, Step, Linear, Cubic };
    struct Track {
        Kind kind{Kind::None};
        float base{}, target{}, rate_base{}, rate_target{}, start{}, duration{1};
        bool active{};
        [[nodiscard]] float value(float time) const {
            if (!active) return target;
            const float length = std::clamp(time - start, 0.0f, duration);
            if (kind == Kind::Step) return duration == 0 || length >= duration ? target : base;
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
    for (std::size_t i = 0; i < tracks.size(); ++i)
        tracks[i].base = tracks[i].target = initial.tracks[i];

    const auto read_u16 = [&](Address at) {
        return static_cast<std::uint16_t>(archive_.s16(at));
    };
    const auto converted = [](std::int16_t raw, unsigned track, bool rate) {
        static constexpr std::array<float, 4> value_scale{
            1.0f / 512.0f, 1.0f / 4.0f, 1.0f / 4096.0f, 1.0f / 16384.0f};
        static constexpr std::array<float, 4> rate_scale{
            1.0f / 512.0f, 1.0f / 32.0f, 1.0f / 8192.0f, 1.0f / 16384.0f};
        unsigned kind{};
        if (track >= 4 && track <= 6) kind = 1;
        else if (track >= 7) kind = 2;
        else if (track == 3) kind = 3;
        return static_cast<float>(raw) * (rate ? rate_scale[kind] : value_scale[kind]);
    };

    Address command = script;
    float cursor{};
    std::size_t budget = 65'536;
    while (budget-- && cursor <= frame) {
        const auto word = read_u16(command);
        const unsigned opcode = word >> 11;
        const unsigned flags = (word >> 1) & 0x3ffU;
        const bool toggle = (word & 1U) != 0;
        command.offset += 2;
        if (opcode == 0) break;

        if (opcode == 13) {
            // The loop offset is relative to its own 16-bit word and is
            // expressed in bytes by the original figatree evaluator.
            const auto relative = archive_.s16(command);
            command.offset = static_cast<std::uint32_t>(static_cast<std::int64_t>(command.offset) + relative);
            continue;
        }
        if (opcode == 12) {
            command.offset += 2; // translation interpolation data offset
            continue;
        }

        float duration{};
        if (toggle) {
            duration = static_cast<float>(read_u16(command));
            command.offset += 2;
        }
        if (opcode == 1) {
            cursor += duration;
            continue;
        }
        if (opcode == 11) continue;
        if (opcode == 14) {
            cursor += duration;
            continue;
        }
        if (opcode == 6) {
            for (unsigned bit = 0; bit < tracks.size(); ++bit) if (flags & (1U << bit)) {
                tracks[bit].rate_target = converted(archive_.s16(command), bit, true);
                command.offset += 2;
            }
            continue;
        }
        if (opcode < 2 || opcode > 10) break;

        for (unsigned bit = 0; bit < tracks.size(); ++bit) if (flags & (1U << bit)) {
            auto& track = tracks[bit];
            track.base = track.target;
            track.target = converted(archive_.s16(command), bit, false);
            command.offset += 2;
            track.start = cursor;
            track.duration = duration;
            track.active = true;
            if (opcode == 2 || opcode == 3) {
                track.kind = Kind::Linear;
                track.rate_base = duration != 0 ? (track.target - track.base) / duration : 0;
                track.rate_target = 0;
            } else if (opcode == 4 || opcode == 5) {
                track.rate_base = track.rate_target;
                track.rate_target = converted(archive_.s16(command), bit, true);
                command.offset += 2;
                track.kind = Kind::Cubic;
            } else if (opcode == 7 || opcode == 8) {
                track.rate_base = track.rate_target;
                track.rate_target = 0;
                track.kind = Kind::Cubic;
            } else {
                track.rate_target = 0;
                track.kind = Kind::Step;
            }
        }
        if (opcode == 2 || opcode == 4 || opcode == 7 || opcode == 9) cursor += duration;
    }
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].active) initial.tracks[i] = tracks[i].value(frame);
    return initial;
}

} // namespace sagas::n64
