#include <sagas/Audio.hpp>
#include <sagas/Fgm.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace sagas {
namespace {

[[noreturn]] void fail(std::string message) {
    if (const char* detail = SDL_GetError(); detail && *detail) message += ": " + std::string(detail);
    throw std::runtime_error(std::move(message));
}
std::uint16_t be16(const std::byte* p) {
    return (std::to_integer<std::uint16_t>(p[0]) << 8) | std::to_integer<std::uint16_t>(p[1]);
}
std::uint32_t be32(const std::byte* p) {
    return (std::to_integer<std::uint32_t>(p[0]) << 24) | (std::to_integer<std::uint32_t>(p[1]) << 16) |
           (std::to_integer<std::uint32_t>(p[2]) << 8) | std::to_integer<std::uint32_t>(p[3]);
}
std::uint32_t le32(const std::byte* p) {
    return std::to_integer<std::uint32_t>(p[0]) | (std::to_integer<std::uint32_t>(p[1]) << 8) |
           (std::to_integer<std::uint32_t>(p[2]) << 16) | (std::to_integer<std::uint32_t>(p[3]) << 24);
}
bool tag(const std::byte* p, const char* text) { return std::memcmp(p, text, 4) == 0; }
double extended80(const std::byte* p) {
    const auto exponent = be16(p);
    std::uint64_t mantissa{};
    for (int i = 0; i < 8; ++i) mantissa = (mantissa << 8) | std::to_integer<unsigned>(p[i + 2]);
    if ((exponent & 0x7fffU) == 0 && mantissa == 0) return 0;
    const double value = std::ldexp(static_cast<double>(mantissa), static_cast<int>(exponent & 0x7fffU) - 16383 - 63);
    return exponent & 0x8000U ? -value : value;
}

struct Pcm { int rate{}; std::vector<std::int16_t> samples; };
Pcm load_aiff(std::span<const std::byte> bytes, float gain) {
    if (bytes.size() < 12 || !tag(bytes.data(), "FORM") ||
        (!tag(bytes.data() + 8, "AIFF") && !tag(bytes.data() + 8, "AIFC")))
        throw std::runtime_error("audio is not AIFF PCM");
    int channels{}, bits{}, rate{};
    std::uint32_t frames{};
    const std::byte* sound{};
    std::size_t sound_size{};
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const auto size = be32(bytes.data() + at + 4);
        const auto body = at + 8;
        if (body + size > bytes.size()) break;
        if (tag(bytes.data() + at, "COMM") && size >= 18) {
            channels = be16(bytes.data() + body); frames = be32(bytes.data() + body + 2);
            bits = be16(bytes.data() + body + 6);
            rate = static_cast<int>(std::lround(extended80(bytes.data() + body + 8)));
            if (size >= 22 && !tag(bytes.data() + body + 18, "NONE"))
                throw std::runtime_error("compressed AIFC is not runtime PCM");
        } else if (tag(bytes.data() + at, "SSND") && size >= 8) {
            const auto offset = be32(bytes.data() + body);
            if (8ULL + offset <= size) { sound = bytes.data() + body + 8 + offset; sound_size = size - 8 - offset; }
        }
        at = body + size + (size & 1U);
    }
    if (!sound || channels < 1 || bits != 16 || rate <= 0) throw std::runtime_error("unsupported AIFF layout");
    const auto count = std::min<std::size_t>(frames, sound_size / 2 / static_cast<std::size_t>(channels));
    Pcm pcm{rate, {}}; pcm.samples.reserve(count);
    for (std::size_t frame = 0; frame < count; ++frame) {
        int mixed{};
        for (int channel = 0; channel < channels; ++channel)
            mixed += static_cast<std::int16_t>(be16(sound + (frame * channels + channel) * 2));
        pcm.samples.push_back(static_cast<std::int16_t>(std::clamp(mixed * gain / channels, -32768.0f, 32767.0f)));
    }
    return pcm;
}

Pcm render_fgm(AssetRepository& assets, std::uint32_t voice_id, float gain) {
    constexpr int output_rate = 32000;
    const auto cue = decode_fgm(assets, voice_id);
    std::unordered_map<int, Pcm> waves;
    auto wave_for = [&](int index) -> const Pcm& {
        if (!waves.contains(index)) {
            std::ostringstream logical;
            // FGM trigger indices address the 44.1 kHz SFX bank.  The same
            // numeric IDs in B1_sounds1 are music instruments (wave 10 is a
            // bowed string), which is why the menu previously sounded like
            // a violin.
            logical << "audio/B1_sounds2/wave_" << std::setw(3) << std::setfill('0') << index << ".aiff";
            waves.emplace(index, load_aiff(*assets.blob(logical.str()), 1.0f));
        }
        return waves.at(index);
    };
    // n_env.c schedules its FGM interpreter every 184 output samples.
    // The extraction AIFF rate is metadata; native FGM playback advances
    // the bank sample at the synthesizer rate times alCents2Ratio(pitch).
    constexpr unsigned samples_per_tick=184;
    const std::size_t count=static_cast<std::size_t>(std::max(cue.end_tick,1))*samples_per_tick;
    std::vector<double> mixed(count);
    for (const auto& voice:cue.voices) {
        const auto& wave=wave_for(voice.wave);
        const std::size_t begin=static_cast<std::size_t>(voice.start_tick)*samples_per_tick;
        const std::size_t end=std::min(count,static_cast<std::size_t>(voice.end_tick)*samples_per_tick);
        double phase=0;
        for (std::size_t frame=begin;frame<end && phase<wave.samples.size();++frame) {
            const float tick=static_cast<float>(frame-begin)/samples_per_tick;
            const auto index=static_cast<std::size_t>(phase);
            const double fraction=phase-index;
            const auto next=std::min(index+1,wave.samples.size()-1);
            const double sample=wave.samples[index]*(1-fraction)+wave.samples[next]*fraction;
            mixed[frame]+=sample*voice.gain*fgm_envelope(voice,tick)*gain*.55;
            phase+=std::pow(2.0,fgm_pitch_cents(voice,static_cast<int>(tick))/1200.0);
        }
    }
    Pcm result{output_rate,std::vector<std::int16_t>(count)};
    for (std::size_t frame=0;frame<count;++frame)
        result.samples[frame]=static_cast<std::int16_t>(std::clamp(mixed[frame],-32768.0,32767.0));
    return result;
}

struct MusicSound {
    int program{}, velocity_min{}, velocity_max{}, key_min{}, key_max{}, key_base{}, detune{}, wave{};
    int instrument_volume{}, sample_volume{}, instrument_pan{}, sample_pan{}, bend_range{};
    int attack_us{}, decay_us{}, release_us{}, attack_volume{}, decay_volume{}, loop_start{}, loop_end{};
};
struct MusicEvent { std::uint32_t tick{}, frame{}; std::uint8_t kind{}, channel{}, a{}, b{}; };
struct MusicPackage { unsigned division{}, tempo{}; std::vector<MusicSound> sounds; std::vector<MusicEvent> events; };

MusicPackage load_music(std::span<const std::byte> bytes) {
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "SGM2", 4) != 0)
        throw std::runtime_error("invalid Sagas music package");
    MusicPackage music{le32(bytes.data()+4), le32(bytes.data()+8), {}, {}};
    const auto sound_count = le32(bytes.data()+12), track_count = le32(bytes.data()+16);
    std::size_t at = 20;
    for (std::uint32_t i = 0; i < sound_count; ++i) {
        if (at + 80 > bytes.size()) throw std::runtime_error("truncated Sagas sound bank");
        std::array<int, 20> v{};
        for (int& field : v) { field = static_cast<int>(le32(bytes.data()+at)); at += 4; }
        music.sounds.push_back({v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11],v[12],
                                v[13],v[14],v[15],v[16],v[17],v[18],v[19]});
    }
    for (std::uint32_t track = 0; track < track_count; ++track) {
        if (at + 16 > bytes.size()) throw std::runtime_error("truncated Sagas track table");
        const auto count = le32(bytes.data()+at+12); at += 16;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (at + 8 > bytes.size()) throw std::runtime_error("truncated Sagas music event");
            music.events.push_back({le32(bytes.data()+at), 0, std::to_integer<std::uint8_t>(bytes[at+4]),
                                    std::to_integer<std::uint8_t>(bytes[at+5]), std::to_integer<std::uint8_t>(bytes[at+6]),
                                    std::to_integer<std::uint8_t>(bytes[at+7])});
            at += 8;
        }
    }
    std::stable_sort(music.events.begin(), music.events.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
    double frame{}; std::uint32_t previous{}; unsigned tempo = music.tempo;
    for (std::size_t i = 0; i < music.events.size();) {
        const auto tick = music.events[i].tick;
        frame += static_cast<double>(tick - previous) * tempo * 32000.0 / (music.division * 1'000'000.0);
        std::size_t end = i;
        while (end < music.events.size() && music.events[end].tick == tick) {
            music.events[end].frame = static_cast<std::uint32_t>(std::llround(frame));
            if (music.events[end].kind == 5)
                tempo = (music.events[end].channel << 16) | (music.events[end].a << 8) | music.events[end].b;
            ++end;
        }
        previous = tick; i = end;
    }
    return music;
}

} // namespace

AudioEngine::AudioEngine(AssetRepository& assets) : assets_(assets) {}
AudioEngine::~AudioEngine() {
    if (music_job_.valid()) music_job_.wait();
    for (auto* stream:motion_streams_) SDL_DestroyAudioStream(stream);
    if (effect_stream_) SDL_DestroyAudioStream(effect_stream_);
    if (music_stream_) SDL_DestroyAudioStream(music_stream_);
}
void AudioEngine::stop() {
    music_loop_.clear();
    for (auto* stream:motion_streams_) SDL_DestroyAudioStream(stream);
    motion_streams_.clear();
    if (effect_stream_) SDL_ClearAudioStream(effect_stream_);
    if (music_stream_) SDL_ClearAudioStream(music_stream_);
}
void AudioEngine::queue(SDL_AudioStream*& stream, std::span<const std::int16_t> samples,
                        int rate, int channels) {
    if (stream) SDL_DestroyAudioStream(stream);
    const SDL_AudioSpec spec{SDL_AUDIO_S16, channels, rate};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) fail("audio device open failed");
    if (!SDL_PutAudioStreamData(stream, samples.data(), static_cast<int>(samples.size_bytes()))) fail("audio queue failed");
    if (!SDL_ResumeAudioStreamDevice(stream)) fail("audio resume failed");
}
void AudioEngine::play(std::string_view logical, float gain) {
    auto pcm = load_aiff(*assets_.blob(logical), gain);
    queue(effect_stream_, pcm.samples, pcm.rate);
}
void AudioEngine::play(AudioCue cue) {
    const auto voice_id = cue == AudioCue::TitlePressStart ? 157U :
                          cue == AudioCue::MenuSelect ? 158U : 164U;
    auto pcm = render_fgm(assets_, voice_id, 1.0f);
    queue(effect_stream_, pcm.samples, pcm.rate);
}
void AudioEngine::play_fgm(unsigned id,float gain) {
    std::erase_if(motion_streams_,[](SDL_AudioStream* stream) {
        if (SDL_GetAudioStreamQueued(stream)>0) return false;
        SDL_DestroyAudioStream(stream);
        return true;
    });
    if (!motion_cache_.contains(id)) {
        auto pcm=render_fgm(assets_,id,1.0f);
        motion_cache_.emplace(id,PreparedAudio{std::move(pcm.samples),pcm.rate,1});
    }
    const auto& pcm=motion_cache_.at(id);
    auto samples=pcm.samples;
    for (auto& sample:samples) sample=static_cast<std::int16_t>(std::clamp(sample*gain,-32768.0f,32767.0f));
    SDL_AudioStream* stream{};
    queue(stream,samples,pcm.rate);
    motion_streams_.push_back(stream);
}
AudioEngine::PreparedAudio AudioEngine::synthesize_music(std::string logical, float gain) {
    const auto bytes = assets_.blob(logical);
    if (bytes->size() >= 16 && std::memcmp(bytes->data(), "SGPC", 4) == 0) {
        const auto rate = le32(bytes->data() + 4);
        const auto channels = le32(bytes->data() + 8);
        const auto count = le32(bytes->data() + 12);
        if (rate == 0 || channels == 0 || channels > 8 || count > (bytes->size() - 16) / 2)
            throw std::runtime_error("invalid baked Sagas PCM package");
        std::vector<std::int16_t> samples(count);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto value = static_cast<std::int16_t>(
                std::to_integer<std::uint16_t>((*bytes)[16 + i * 2]) |
                (std::to_integer<std::uint16_t>((*bytes)[17 + i * 2]) << 8));
            samples[i] = static_cast<std::int16_t>(std::clamp(value * gain, -32768.0f, 32767.0f));
        }
        std::size_t loop_begin=0,loop_end=0;
        const std::size_t tail=16+static_cast<std::size_t>(count)*2;
        if (bytes->size()>=tail+12 && tag(bytes->data()+tail,"LOOP")) {
            loop_begin=static_cast<std::size_t>(le32(bytes->data()+tail+4))*channels;
            loop_end=static_cast<std::size_t>(le32(bytes->data()+tail+8))*channels;
            if (loop_begin>=loop_end || loop_end>samples.size()) throw std::runtime_error("invalid PCM loop bounds");
        }
        return {std::move(samples), static_cast<int>(rate), static_cast<int>(channels),loop_begin,loop_end};
    }
    const auto package = load_music(*bytes);
    struct Channel { int program{}, volume{127}, pan{64}, bend{8192}, bend_range{200}; bool sustain{}; };
    struct Voice {
        const MusicSound* sound{}; const Pcm* pcm{}; int channel{}, note{}, velocity{};
        double position{}, base_step{}; std::uint64_t age{}, release_age{};
        bool released{}, sustained{};
    };
    std::array<Channel, 16> channels{};
    if (const auto base = std::find_if(package.sounds.begin(), package.sounds.end(), [](const auto& s){ return s.program == 0; });
        base != package.sounds.end())
        for (auto& channel : channels) { channel.pan = base->instrument_pan; channel.bend_range = base->bend_range; }
    int master_volume = 127;
    std::unordered_map<int, Pcm> waves;
    std::vector<Voice> voices;
    std::vector<std::int16_t> output;
    const auto final_frame = (package.events.empty() ? 0U : package.events.back().frame) + 160000U;
    output.reserve(static_cast<std::size_t>(final_frame) * 2);
    std::size_t event_index{};
    auto wave = [&](int id) -> const Pcm* {
        if (!waves.contains(id)) {
            std::ostringstream name; name << "audio/B1_sounds1/wave_" << std::setw(3) << std::setfill('0') << id << ".aiff";
            waves.emplace(id, load_aiff(*assets_.blob(name.str()), 1.0f));
        }
        return &waves.at(id);
    };
    for (std::uint32_t frame = 0; frame < final_frame; ++frame) {
        while (event_index < package.events.size() && package.events[event_index].frame <= frame) {
            const auto& event = package.events[event_index++];
            auto& channel = channels[event.channel & 15];
            if (event.kind == 2) {
                channel.program = event.a;
                if (const auto sound = std::find_if(package.sounds.begin(), package.sounds.end(), [&](const auto& s){ return s.program == channel.program; }); sound != package.sounds.end())
                    channel.bend_range = sound->bend_range;
            } else if (event.kind == 3) {
                if (event.a == 7) channel.volume = event.b;
                else if (event.a == 10) channel.pan = event.b;
                else if (event.a == 20) channel.bend_range = event.b >= 121 ? 1200 : event.b * 10;
                else if (event.a == 21) master_volume = event.b;
                else if (event.a == 64) {
                    channel.sustain = event.b > 63;
                    if (!channel.sustain) for (auto& voice : voices)
                        if (voice.channel == (event.channel & 15) && voice.sustained) { voice.sustained = false; voice.released = true; voice.release_age = 0; }
                }
            } else if (event.kind == 4) channel.bend = event.a | (event.b << 7);
            else if (event.kind == 1 || (event.kind == 0 && event.b == 0)) {
                const auto found = std::find_if(voices.begin(), voices.end(), [&](const auto& v) {
                    return v.channel == (event.channel & 15) && v.note == event.a && !v.released && !v.sustained;
                });
                if (found != voices.end()) {
                    if (channel.sustain) found->sustained = true;
                    else { found->released = true; found->release_age = 0; }
                }
            } else if (event.kind == 0) {
                const auto sound = std::find_if(package.sounds.begin(), package.sounds.end(), [&](const auto& item) {
                    return item.program == channel.program && event.a >= item.key_min && event.a <= item.key_max &&
                           event.b >= item.velocity_min && event.b <= item.velocity_max;
                });
                if (sound != package.sounds.end()) {
                    const auto* pcm = wave(sound->wave);
                    const float cents = (event.a - sound->key_base) * 100.0f + sound->detune;
                    voices.push_back({&*sound, pcm, event.channel & 15, event.a, event.b, 0,
                                      std::pow(2.0, cents / 1200.0) * pcm->rate / 32000.0, 0, 0, false, false});
                }
            }
        }
        double left{}, right{};
        for (auto& voice : voices) {
            if (!voice.pcm || voice.position >= voice.pcm->samples.size()) continue;
            const auto& sound = *voice.sound;
            const auto& channel = channels[voice.channel];
            const double age_us = voice.age * (1'000'000.0 / 32000.0);
            float envelope = sound.decay_volume / 127.0f;
            if (sound.attack_us > 0 && age_us < sound.attack_us)
                envelope = static_cast<float>(age_us / sound.attack_us) * sound.attack_volume / 127.0f;
            else if (sound.decay_us > 0 && age_us < sound.attack_us + sound.decay_us) {
                const float mix = static_cast<float>((age_us - sound.attack_us) / sound.decay_us);
                envelope = (sound.attack_volume + (sound.decay_volume - sound.attack_volume) * mix) / 127.0f;
            }
            if (voice.released) {
                const double release_us = voice.release_age++ * (1'000'000.0 / 32000.0);
                envelope *= sound.release_us > 0 ? std::max(0.0, 1.0 - release_us / sound.release_us) : 0.0;
                if (envelope <= 0) { voice.pcm = nullptr; continue; }
            }
            const auto index = static_cast<std::size_t>(voice.position);
            const auto next = std::min(index + 1, voice.pcm->samples.size() - 1);
            const double fraction = voice.position - index;
            const double sample = voice.pcm->samples[index] * (1-fraction) + voice.pcm->samples[next] * fraction;
            const double level = gain * voice.velocity / 127.0 * channel.volume / 127.0 * master_volume / 127.0 *
                                 sound.instrument_volume / 127.0 * sound.sample_volume / 127.0 * envelope;
            const int pan = std::clamp(channel.pan - 64 + sound.sample_pan, 0, 127);
            const double angle = pan * (1.5707963267948966 / 127.0);
            left += sample * level * std::cos(angle); right += sample * level * std::sin(angle);
            const float bend_cents = (channel.bend - 8192) * (channel.bend_range / 8192.0f);
            voice.position += voice.base_step * std::pow(2.0, bend_cents / 1200.0); ++voice.age;
            if (!voice.released && sound.loop_end > sound.loop_start && voice.position >= sound.loop_end)
                voice.position = sound.loop_start + std::fmod(voice.position - sound.loop_start, sound.loop_end - sound.loop_start);
        }
        if ((frame & 4095U) == 0)
            voices.erase(std::remove_if(voices.begin(), voices.end(), [](const auto& voice){ return !voice.pcm; }), voices.end());
        output.push_back(static_cast<std::int16_t>(std::clamp(left, -32768.0, 32767.0)));
        output.push_back(static_cast<std::int16_t>(std::clamp(right, -32768.0, 32767.0)));
    }
    return {std::move(output), 32000, 2};
}

void AudioEngine::preload_music(std::string_view logical, float gain) {
    const std::string name(logical);
    if (music_job_.valid() && music_job_name_ == name && music_job_gain_ == gain) return;
    if (music_job_.valid()) (void)music_job_.get();
    music_job_name_ = name;
    music_job_gain_ = gain;
    music_job_ = std::async(std::launch::async, [this, name, gain] {
        return synthesize_music(name, gain);
    });
}

bool AudioEngine::music_ready(std::string_view logical) const {
    return music_job_.valid() && music_job_name_ == logical &&
           music_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void AudioEngine::play_music(std::string_view logical, float gain, bool loop) {
    PreparedAudio prepared;
    if (music_job_.valid() && music_job_name_ == logical && music_job_gain_ == gain) {
        prepared = music_job_.get();
        music_job_name_.clear();
    } else {
        prepared = synthesize_music(std::string(logical), gain);
    }
    music_loop_.clear();
    if (loop) {
        if (prepared.loop_end<=prepared.loop_begin) throw std::runtime_error("music has no authored loop data");
        music_loop_.assign(prepared.samples.begin()+prepared.loop_begin,prepared.samples.begin()+prepared.loop_end);
        prepared.samples.resize(prepared.loop_end);
    }
    music_bytes_=prepared.samples.size()*sizeof(std::int16_t);
    music_bytes_per_second_=prepared.rate*prepared.channels*sizeof(std::int16_t);
    queue(music_stream_, prepared.samples, prepared.rate, prepared.channels);
    music_started_=std::chrono::steady_clock::now();
}

void AudioEngine::update() {
    if (!music_stream_ || music_loop_.empty()) return;
    // Queue ahead of the boundary, excluding the baked release/silence tail.
    if (SDL_GetAudioStreamQueued(music_stream_)<music_bytes_per_second_*2) {
        const auto bytes=music_loop_.size()*sizeof(std::int16_t);
        if (!SDL_PutAudioStreamData(music_stream_,music_loop_.data(),static_cast<int>(bytes))) fail("music loop queue failed");
        music_bytes_+=bytes;
    }
}

double AudioEngine::music_seconds() const {
    if (!music_stream_ || music_bytes_per_second_<=0) return 0;
    const auto queued=SDL_GetAudioStreamQueued(music_stream_);
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-music_started_).count();
    if (queued<0) return elapsed;
    const double played=static_cast<double>(music_bytes_-std::min(music_bytes_,static_cast<std::size_t>(queued)))/music_bytes_per_second_;
    return queued==0 ? std::max(played,elapsed) : played;
}

} // namespace sagas
