#pragma once

#include <sagas/Core.hpp>

namespace sagas {

struct FgmEnvelopePoint {
    int tick{};
    float volume{1};
};

struct FgmPitchPoint {
    int tick{};
    float cents{};
};

struct FgmVoice {
    int wave{}, start_tick{}, end_tick{};
    float gain{1};
    std::vector<FgmEnvelopePoint> envelope;
    std::vector<FgmPitchPoint> pitch;
};

struct FgmCue {
    int end_tick{};
    std::vector<FgmVoice> voices;
};

[[nodiscard]] float fgm_pitch_cents(const FgmVoice& voice, int tick) noexcept;
[[nodiscard]] float fgm_envelope(const FgmVoice& voice, float tick) noexcept;

// Decodes the original fgm.ucd voice script and its fgm.tbl articulation.
[[nodiscard]] FgmCue decode_fgm(AssetRepository& assets, std::uint32_t voice_id);

} // namespace sagas
