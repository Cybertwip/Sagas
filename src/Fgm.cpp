#include <sagas/Fgm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace sagas {
namespace {

using Bytes = std::span<const std::byte>;

std::uint8_t u8(Bytes data, std::size_t& at) {
    if (at >= data.size()) throw std::runtime_error("truncated FGM bytecode");
    return std::to_integer<std::uint8_t>(data[at++]);
}

std::uint32_t be32(const std::byte* data) {
    return (std::to_integer<std::uint32_t>(data[0]) << 24) |
           (std::to_integer<std::uint32_t>(data[1]) << 16) |
           (std::to_integer<std::uint32_t>(data[2]) << 8) |
           std::to_integer<std::uint32_t>(data[3]);
}

int varint(Bytes data, std::size_t& at) {
    const int first = u8(data, at);
    return first & 0x80 ? ((first & 0x7f) << 8) | u8(data, at) : first;
}

Bytes entry(Bytes file, std::uint32_t index) {
    if (file.size() < 8) throw std::runtime_error("invalid FGM table");
    const auto count = be32(file.data());
    if (index >= count || file.size() < 4 + count * 4ULL) throw std::runtime_error("FGM index out of range");
    const auto begin = be32(file.data() + 4 + index * 4ULL);
    const auto end = index + 1 < count ? be32(file.data() + 8 + index * 4ULL)
                                       : static_cast<std::uint32_t>(file.size());
    if (begin > end || end > file.size()) throw std::runtime_error("invalid FGM entry offsets");
    return file.subspan(begin, end - begin);
}

struct Articulation {
    int wave{-1}, pitch{}, duration{};
    std::vector<FgmEnvelopePoint> envelope{{0, 1}};
    std::vector<FgmPitchPoint> pitch_events;
};

Articulation decode_articulation(Bytes table, int index) {
    Articulation result;
    const auto code = entry(table, index);
    std::size_t at{};
    int tick{}, volume = 127;
    while (at < code.size()) {
        const auto instruction = u8(code, at);
        int wait = instruction & 0xf;
        if (wait & 8) {
            const int extension = u8(code, at);
            wait = ((wait & 7) << 7) | (extension & 0x7f);
            if (extension & 0x80) wait = (wait << 8) | u8(code, at);
        }
        switch (instruction & 0xf0) {
            case 0x00: {
                const int value = u8(code, at);
                volume = value <= 127 ? value : std::clamp(volume + value - 192, 0, 127);
                result.envelope.push_back({tick, volume / 127.0f});
                break;
            }
            case 0x10: (void)u8(code, at); break;
            case 0x20: {
                int value = (u8(code, at) << 8) | u8(code, at);
                if (value & 0x8000) value -= 0x10000;
                if (value <= 1200) result.pitch = std::max(value, -1200);
                else result.pitch = std::clamp(result.pitch + value - 2400, -1200, 1200);
                result.pitch_events.push_back({tick,static_cast<float>(result.pitch)});
                break;
            }
            case 0x30: (void)u8(code, at); break;
            case 0x40: (void)u8(code, at); (void)varint(code, at); break;
            case 0x50: (void)u8(code, at); break;
            case 0x60: result.wave = varint(code, at); break;
            case 0x70: result.duration = tick; return result;
            case 0x80: break;
            case 0x90: return result; // looping articulations hold their current state
            default: throw std::runtime_error("unknown FGM articulation opcode");
        }
        tick += wait;
    }
    result.duration = tick;
    return result;
}

void decode_voice(Bytes ucd, Bytes table, std::uint32_t voice_id, int base_tick,
                  FgmCue& cue, int depth) {
    if (depth > 8) throw std::runtime_error("FGM fork depth exceeded");
    const auto code = entry(ucd, voice_id);
    std::size_t at{};
    std::array<int, 6> durations{};
    const auto first_voice = cue.voices.size();
    std::optional<std::size_t> active_voice;

    int tick = base_tick, articulation{}, volume = 127, transpose{};
    while (at < code.size()) {
        const auto instruction = u8(code, at);
        if ((instruction & 0xf8) < 0xd0) {
            const int pitch_code = instruction >> 3;
            const int duration_code = instruction & 7;
            const int duration = duration_code == 7 ? varint(code, at) :
                                 duration_code == 0 ? 0 : durations[duration_code - 1];
            if (pitch_code == 0) {
                if (active_voice) cue.voices[*active_voice].end_tick = tick;
                active_voice.reset();
            } else {
                const float note_pitch = static_cast<float>(pitch_code * 100 - 1300 + transpose);
                if (active_voice) {
                    auto& voice = cue.voices[*active_voice];
                    voice.pitch.push_back({tick - voice.start_tick, note_pitch});
                } else {
                    const auto art = decode_articulation(table, articulation);
                    if (art.wave >= 0) {
                        cue.voices.push_back({art.wave, tick, 0, volume / 255.0f,
                                             art.envelope, {{0, note_pitch}}, art.pitch_events});
                        active_voice = cue.voices.size() - 1;

                    }
                }
            }
            transpose = 0;
            tick += duration;
            continue;
        }
        switch (instruction) {
            case 0xd0:
                cue.end_tick = std::max(cue.end_tick, tick);
                if (active_voice && cue.voices[*active_voice].end_tick == 0)
                    cue.voices[*active_voice].end_tick = tick;
                for (std::size_t i = first_voice; i < cue.voices.size(); ++i)
                    if (cue.voices[i].end_tick == 0) cue.voices[i].end_tick = tick;
                return;
            case 0xd1: articulation = varint(code, at); break;
            case 0xd2: case 0xd3: case 0xdc: case 0xde: (void)u8(code, at); break;
            case 0xd4: for (auto& duration : durations) duration = varint(code, at); break;
            case 0xd5: volume = u8(code, at); break;
            case 0xd6: volume = std::clamp(volume + static_cast<std::int8_t>(u8(code, at)), 0, 255); break;
            case 0xd7: (void)u8(code, at); break;
            case 0xd8: (void)u8(code, at); break;
            case 0xd9: decode_voice(ucd, table, varint(code, at), tick, cue, depth + 1); break;
            case 0xda: case 0xdb: break;
            case 0xdd: (void)u8(code, at); break;
            case 0xdf: transpose -= 2400; break;
            case 0xe0: transpose -= 4800; break;
            default: throw std::runtime_error("unknown FGM voice opcode");
        }
    }
    cue.end_tick = std::max(cue.end_tick, tick);
}

} // namespace

float fgm_pitch_cents(const FgmVoice& voice,int tick) noexcept {
    float value=0;
    for (const auto& point:voice.pitch) {if (point.tick>tick) break;value=point.cents;}
    float articulation=0;
    for (const auto& point:voice.articulation_pitch) {if (point.tick>tick) break;articulation=point.cents;}
    return value+articulation;
}
float fgm_envelope(const FgmVoice& voice,float tick) noexcept {
    if (voice.envelope.empty()) return 1;
    auto previous=voice.envelope.front();
    for (const auto& point:voice.envelope) {
        if (point.tick>tick) {
            if (point.tick==previous.tick) return point.volume;
            const float fraction=std::clamp((tick-previous.tick)/(point.tick-previous.tick),0.0f,1.0f);
            return previous.volume+(point.volume-previous.volume)*fraction;
        }
        previous=point;
    }
    return previous.volume;
}

FgmCue decode_fgm(AssetRepository& assets, std::uint32_t voice_id) {
    const auto ucd = assets.blob("audio/fgm.ucd.bin");
    const auto table = assets.blob("audio/fgm.tbl.bin");
    FgmCue cue;
    decode_voice(*ucd, *table, voice_id, 0, cue, 0);
    for (auto& voice : cue.voices) if (voice.end_tick == 0) voice.end_tick = cue.end_tick;
    return cue;
}

} // namespace sagas
