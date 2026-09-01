#pragma once

#include <sagas/Core.hpp>

namespace sagas {

struct FgmEnvelopePoint {
    int tick{};
    float volume{1};
};

struct FgmVoice {
    int wave{}, start_tick{}, end_tick{};
    float cents{}, gain{1};
    std::vector<FgmEnvelopePoint> envelope;
};

struct FgmCue {
    int end_tick{};
    std::vector<FgmVoice> voices;
};

// Decodes the original fgm.ucd voice script and its fgm.tbl articulation.
[[nodiscard]] FgmCue decode_fgm(AssetRepository& assets, std::uint32_t voice_id);

} // namespace sagas
