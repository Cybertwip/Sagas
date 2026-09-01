#pragma once

#include <sagas/Core.hpp>

struct SDL_AudioStream;

namespace sagas {

class AudioEngine final {
public:
    explicit AudioEngine(AssetRepository& assets);
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    void play(std::string_view logical, float gain = 1.0f);
    void play_music(std::string_view logical, float gain = 1.0f);
    void stop();
private:
    void queue(std::span<const std::int16_t> samples, int rate, int channels = 1);
    AssetRepository& assets_;
    SDL_AudioStream* stream_{};
};

} // namespace sagas
