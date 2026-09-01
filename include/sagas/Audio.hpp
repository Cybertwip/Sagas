#pragma once

#include <sagas/Core.hpp>

#include <future>

struct SDL_AudioStream;

namespace sagas {

enum class AudioCue {
    TitlePressStart,
    MenuSelect,
    MenuScroll,
};

class AudioEngine final {
public:
    explicit AudioEngine(AssetRepository& assets);
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    void play(std::string_view logical, float gain = 1.0f);
    void play(AudioCue cue);
    void preload_music(std::string_view logical, float gain = 1.0f);
    [[nodiscard]] bool music_ready(std::string_view logical) const;
    void play_music(std::string_view logical, float gain = 1.0f);
    void stop();
private:
    struct PreparedAudio {
        std::vector<std::int16_t> samples;
        int rate{}, channels{};
    };
    [[nodiscard]] PreparedAudio synthesize_music(std::string logical, float gain);
    static void queue(SDL_AudioStream*& stream, std::span<const std::int16_t> samples,
                      int rate, int channels = 1);
    AssetRepository& assets_;
    SDL_AudioStream* music_stream_{};
    SDL_AudioStream* effect_stream_{};
    std::future<PreparedAudio> music_job_;
    std::string music_job_name_;
    float music_job_gain_{};
};

} // namespace sagas
