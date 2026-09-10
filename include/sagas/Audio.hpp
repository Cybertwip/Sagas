#pragma once

#include <sagas/Core.hpp>

#include <future>
#include <unordered_map>

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
    void play_fgm(unsigned id, float gain = 1.0f, float pitch = 0.0f);
    void play_character_fgm(std::string_view model, unsigned id);
    void preload_music(std::string_view logical, float gain = 1.0f);
    [[nodiscard]] bool music_ready(std::string_view logical) const;
    void play_music(std::string_view logical, float gain = 1.0f, bool loop = false);
    void update();
    [[nodiscard]] bool music_looping() const { return !music_loop_.empty(); }
    void stop();
    [[nodiscard]] double music_seconds() const;
private:
    struct PreparedAudio {
        std::vector<std::int16_t> samples;
        int rate{}, channels{};
        std::size_t loop_begin{},loop_end{};
    };
    [[nodiscard]] PreparedAudio synthesize_music(std::string logical, float gain);
    static void queue(SDL_AudioStream*& stream, std::span<const std::int16_t> samples,
                      int rate, int channels = 1);
    AssetRepository& assets_;
    SDL_AudioStream* music_stream_{};
    std::vector<std::int16_t> music_loop_;
    SDL_AudioStream* effect_stream_{};
    std::vector<SDL_AudioStream*> motion_streams_;
    std::unordered_map<unsigned,PreparedAudio> motion_cache_;
    std::future<PreparedAudio> music_job_;
    std::string music_job_name_;
    float music_job_gain_{};
    std::size_t music_bytes_{};
    int music_bytes_per_second_{};
    std::chrono::steady_clock::time_point music_started_{};
};

} // namespace sagas
